// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2024 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <hw_access/mali_kbase_hw_access.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>
#include <mali_kbase_ctx_sched.h>
#include <mmu/mali_kbase_mmu_faults_decoder.h>

bool kbase_is_gpu_removed(struct kbase_device *kbdev)
{
	if (!IS_ENABLED(CONFIG_MALI_ARBITER_SUPPORT))
		return false;


	return (KBASE_REG_READ(kbdev, GPU_CONTROL_ENUM(GPU_ID)) == 0);
}

#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
#include <platform/mtk_platform_common/mtk_platform_logbuffer.h>
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */

#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
#include <ged_mali_event.h>
#include <platform/mtk_platform_common/mtk_platform_mali_event.h>
#endif /* CONFIG_MALI_MTK_MBRAIN_SUPPORT */

/**
 * kbase_report_gpu_fault - Report a GPU fault of the device.
 *
 * @kbdev:    Kbase device pointer
 * @status:   Fault status
 * @as_nr:    Faulty address space
 * @as_valid: true if address space is valid
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 */
static void kbase_report_gpu_fault(struct kbase_device *kbdev, u32 status, u32 as_nr, bool as_valid)
{
	u64 address = kbase_reg_read64(kbdev, GPU_CONTROL_ENUM(GPU_FAULTADDRESS));

	/* Report GPU fault for all contexts in case either
	 * the address space is invalid or it's MCU address space.
	 */
	kbase_mmu_gpu_fault_interrupt(kbdev, status, as_nr, address, as_valid);
}

static void kbase_gpu_fault_interrupt(struct kbase_device *kbdev)
{
	const u32 status = kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(GPU_FAULTSTATUS));
	const bool as_valid = status & GPU_FAULTSTATUS_JASID_VALID_MASK;
	const u32 as_nr = (status & GPU_FAULTSTATUS_JASID_MASK) >> GPU_FAULTSTATUS_JASID_SHIFT;
	bool bus_fault = (status & GPU_FAULTSTATUS_EXCEPTION_TYPE_MASK) ==
			 GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_BUS_FAULT;

	if (bus_fault) {
		/* If as_valid, reset gpu when ASID is for MCU. */
		if (!as_valid || (as_nr == MCU_AS_NR)) {
			kbase_report_gpu_fault(kbdev, status, as_nr, as_valid);

			dev_err(kbdev->dev, "GPU bus fault triggering gpu-reset ...\n");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
			mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
				"GPU bus fault triggering gpu-reset ...\n");
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */
			if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
				ged_mali_event_update_gpu_reset_nolock(GPU_RESET_BUS_FAULT);
#endif /* CONFIG_MALI_MTK_MBRAIN_SUPPORT */
				kbase_reset_gpu(kbdev);
			}
		} else {
			/* Handle Bus fault */
			if (kbase_mmu_bus_fault_interrupt(kbdev, status, as_nr)) {
				dev_warn(kbdev->dev, "fail to handle GPU bus fault ...\n");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
				mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
					"fail to handle GPU bus fault ...\n");
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */
			}
		}
	} else
		kbase_report_gpu_fault(kbdev, status, as_nr, as_valid);

}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	u32 power_changed_mask = (POWER_CHANGED_ALL | MCU_STATUS_GPU_IRQ);
	struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;


	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, val);
	if (val & GPU_FAULT)
		kbase_gpu_fault_interrupt(kbdev);

	if (val & GPU_PROTECTED_FAULT) {
		unsigned long flags;

		dev_err_ratelimited(kbdev->dev, "GPU fault in protected mode");
#if IS_ENABLED(CONFIG_MALI_MTK_LOG_BUFFER)
		mtk_logbuffer_type_print(kbdev, MTK_LOGBUFFER_TYPE_CRITICAL | MTK_LOGBUFFER_TYPE_EXCEPTION,
			"GPU fault in protected mode\n");
#endif /* CONFIG_MALI_MTK_LOG_BUFFER */
#if IS_ENABLED(CONFIG_MALI_MTK_DEBUG_DUMP)
		queue_work(kbdev->mtk_enop_metadata_dump_workq, &kbdev->mtk_enop_metadata_dump_work);
#endif /* CONFIG_MALI_MTK_DEBUG_DUMP */

		/* Mask the protected fault interrupt to avoid the potential
		 * deluge of such interrupts. It will be unmasked on GPU reset.
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_MASK),
				  GPU_IRQ_REG_ALL & ~GPU_PROTECTED_FAULT);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		kbase_csf_scheduler_spin_lock(kbdev, &flags);
		if (!WARN_ON(!kbase_csf_scheduler_protected_mode_in_use(kbdev))) {
			struct base_gpu_queue_group_error const
				err_payload = { .error_type = BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
						.payload = {
							.fatal_group = {
								.status =
									GPU_EXCEPTION_TYPE_SW_FAULT_0,
							} } };

			kbase_debug_csf_fault_notify(kbdev, scheduler->active_protm_grp->kctx,
						     DF_GPU_PROTECTED_FAULT);

			scheduler->active_protm_grp->faulted = true;
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
			ged_mali_event_update_pmode_flag_nolock(DEVICE_LOST_PMODE_FAULT);
#endif /* CONFIG_MALI_MTK_MBRAIN_SUPPORT */
			kbase_csf_add_group_fatal_error(scheduler->active_protm_grp, &err_payload);
			kbase_event_wakeup(scheduler->active_protm_grp->kctx);
		}
		kbase_csf_scheduler_spin_unlock(kbdev, flags);

		if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR)) {
#if IS_ENABLED(CONFIG_MALI_MTK_MBRAIN_SUPPORT)
			ged_mali_event_update_gpu_reset_nolock(GPU_RESET_PMODE_FAULT);
#endif /* CONFIG_MALI_MTK_MBRAIN_SUPPORT */
			kbase_reset_gpu(kbdev);
		}

		/* Defer the clearing to the GPU reset sequence */
		val &= ~GPU_PROTECTED_FAULT;
	}

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	/* Defer clearing CLEAN_CACHES_COMPLETED to kbase_clean_caches_done.
	 * We need to acquire hwaccess_lock to avoid a race condition with
	 * kbase_gpu_cache_flush_and_busy_wait
	 */
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, val & ~CLEAN_CACHES_COMPLETED);
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(GPU_IRQ_CLEAR), val & ~CLEAN_CACHES_COMPLETED);

#ifdef KBASE_PM_RUNTIME
	if (val & DOORBELL_MIRROR) {
		unsigned long flags;

		dev_dbg(kbdev->dev, "Doorbell mirror interrupt received");

		/* Assume that the doorbell comes from userspace which
		 * presents new works in order to invalidate a possible GPU
		 * idle event.
		 * If the doorbell was raised by KBase then the FW would handle
		 * the pending doorbell then raise a 2nd GBL_IDLE IRQ which
		 * would allow us to put the GPU to sleep.
		 */
		atomic_set(&scheduler->gpu_no_longer_idle, true);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_pm_disable_db_mirror_interrupt(kbdev);

		if (likely(kbdev->pm.backend.mcu_state == KBASE_MCU_IN_SLEEP)) {
			if (IS_ENABLED(CONFIG_MALI_DEBUG)) {
				u32 const mcu_status =
					kbase_reg_read32(kbdev, GPU_CONTROL_ENUM(MCU_STATUS));
				WARN_ON_ONCE(MCU_STATUS_VALUE_GET(mcu_status) !=
					     MCU_STATUS_VALUE_HALT);
			}

			kbdev->pm.backend.exit_gpu_sleep_mode = true;
#if IS_ENABLED(CONFIG_MALI_MTK_ADAPTIVE_POWER_POLICY)
			kbdev->csf.scheduler.keep_apo_timer = true;
#endif
			kbase_csf_scheduler_invoke_tick(kbdev);
		} else if (atomic_read(&kbdev->csf.scheduler.fw_soi_enabled) &&
			   (kbdev->pm.backend.mcu_state != KBASE_MCU_ON_PEND_SLEEP)) {
			/* Ensure that the MCU has become halted/not enabled
			 * before re-enabling DB notification, otherwise FW
			 * might not have had a chance to go to sleep after
			 * having issued a HALT request. This could cause
			 * issues if the MCU becomes halted later unexpectedly.
			 * This wait is expected to complete instantly in all
			 * cases so timeouts are tolerable.
			 */
			u32 mcu_status;
			const u32 timeout_us =
				kbase_get_timeout_ms(kbdev, CSF_FIRMWARE_SOI_HALT_TIMEOUT) *
				USEC_PER_MSEC;
			int err = kbase_reg_poll32_timeout(
				kbdev, GPU_CONTROL_ENUM(MCU_STATUS), mcu_status,
				MCU_STATUS_VALUE_GET(mcu_status) != MCU_STATUS_VALUE_ENABLED, 1,
				timeout_us, false);
			if (unlikely(err))
				dev_warn(kbdev->dev, "MCU hasn't halted after automatic sleep");

			/* The firmware is going to sleep on its own but new
			 * doorbells were rung before we manage to handle
			 * the GLB_IDLE IRQ in the bottom half. We shall enable
			 * DB notification to allow the DB to be handled by FW.
			 */
			dev_dbg(kbdev->dev, "Re-enabling MCU immediately following DB_MIRROR IRQ");
			kbase_pm_enable_mcu_db_notification(kbdev);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif

	/* kbase_pm_check_transitions (called by kbase_pm_power_changed) must
	 * be called after the IRQ has been cleared. This is because it might
	 * trigger further power transitions and we don't want to miss the
	 * interrupt raised to notify us that these further transitions have
	 * finished. The same applies to kbase_clean_caches_done() - if another
	 * clean was queued, it might trigger another clean, which might
	 * generate another interrupt which shouldn't be missed.
	 */

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	if (val & power_changed_mask) {
		kbase_pm_power_changed(kbdev);
	} else if (val & CLEAN_CACHES_COMPLETED) {
		/* If cache line evict messages can be lost when shader cores
		 * power down then we need to flush the L2 cache before powering
		 * down cores. When the flush completes, the shaders' state
		 * machine needs to be re-invoked to proceed with powering down
		 * cores.
		 */
		if (kbdev->pm.backend.l2_always_on ||
		    kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_921))
			kbase_pm_power_changed(kbdev);
	}

	if (val & MCU_STATUS_GPU_IRQ)
		wake_up_all(&kbdev->csf.event_wait);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, val);
}
KBASE_EXPORT_TEST_API(kbase_gpu_interrupt);

