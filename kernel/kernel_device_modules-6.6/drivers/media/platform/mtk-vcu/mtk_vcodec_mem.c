// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Yunfei Dong <yunfei.dong@mediatek.com>
 */

#include <linux/dma-heap.h>
#include <uapi/linux/dma-heap.h>
#include "mtk_heap.h"
#include "mtk_vcodec_mem.h"

#define vcu_mem_dbg_log(fmt, arg...) do { \
		if (vcu_queue->enable_vcu_dbg_log) \
			pr_info(fmt, ##arg); \
	} while (0)

#undef pr_debug
#define pr_debug vcu_mem_dbg_log

static uint64_t mtk_vcu_va_cnt;

static void vcu_buf_remove(struct mtk_vcu_queue *vcu_queue, unsigned int buffer)
{
	unsigned int last_buffer;

	last_buffer = vcu_queue->num_buffers - 1U;
	if (last_buffer != buffer)
		vcu_queue->bufs[buffer] = vcu_queue->bufs[last_buffer];
	vcu_queue->bufs[last_buffer].mem_priv = NULL;
	vcu_queue->bufs[last_buffer].size = 0;
	vcu_queue->bufs[last_buffer].dbuf = NULL;
	atomic_set(&vcu_queue->bufs[last_buffer].ref_cnt, 0);
	vcu_queue->num_buffers--;
}

struct mtk_vcu_queue *mtk_vcu_mem_init(struct device *dev,
	struct cmdq_client *cmdq_clt)
{
	struct mtk_vcu_queue *vcu_queue = NULL;

	vcu_queue = kvmalloc(sizeof(struct mtk_vcu_queue), GFP_KERNEL | __GFP_ZERO);
	if (vcu_queue == NULL)
		return NULL;
	INIT_LIST_HEAD(&vcu_queue->pa_pages.list);
	vcu_queue->mem_ops = &vb2_dma_contig_memops;
	vcu_queue->dev = dev;
	vcu_queue->cmdq_clt = cmdq_clt;
	vcu_queue->num_buffers = 0;
	vcu_queue->map_buf_pa = 0;
	mutex_init(&vcu_queue->mmap_lock);
	mutex_init(&vcu_queue->dev_lock);

	vcu_queue->vb_queue.mem_ops         = &vb2_dma_contig_memops;
	vcu_queue->vb_queue.dma_dir         = 0;
	vcu_queue->vb_queue.dma_attrs       = 0;
	vcu_queue->vb_queue.gfp_flags       = 0;
/*
 *	vcu_queue->vb_queue.type            = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
 *	vcu_queue->vb_queue.io_modes        = VB2_DMABUF | VB2_MMAP;
 *	vcu_queue->vb_queue.drv_priv        = ctx;
 *	vcu_queue->vb_queue.buf_struct_size = sizeof(struct mtk_video_dec_buf);
 *	vcu_queue->vb_queue.ops             = &mtk_vdec_vb2_ops;
 *	vcu_queue->vb_queue.mem_ops         = &vb2_dma_contig_memops;
 *	vcu_queue->vb_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
 *	vcu_queue->vb_queue.lock            = &ctx->q_mutex;
 *	vcu_queue->vb_queue.dev             = &ctx->dev->plat_dev->dev;
 *	vcu_queue->vb_queue.allow_zero_bytesused = 1;
 *	ret = vb2_queue_init(&vcu_queue->vb_queue);
 *	if (ret) {
 *		mtk_v4l2_err("Failed to initialize videobuf2 queue(capture)");
 *	}
 */
	return vcu_queue;
}

void mtk_vcu_mem_release(struct mtk_vcu_queue *vcu_queue)
{
	struct mtk_vcu_mem *vcu_buffer;
	struct mtk_vcu_sec_handle *vcu_sec_handle;
	unsigned int buffer, handle;
	struct vcu_pa_pages *tmp;
	struct list_head *p, *q;

	mutex_lock(&vcu_queue->mmap_lock);
	pr_debug("Release vcu queue !\n");
	if (vcu_queue->num_buffers != 0) {
		for (buffer = 0; buffer < vcu_queue->num_buffers; buffer++) {
			vcu_buffer = &vcu_queue->bufs[buffer];
			if (vcu_buffer->dbuf == NULL)
				vcu_queue->mem_ops->put(vcu_buffer->mem_priv);
			else
				fput(vcu_buffer->dbuf->file);

			pr_debug("Free %d dbuf = %p size = %d mem_priv = %lx ref_cnt = %d\n",
				 buffer, vcu_buffer->dbuf,
				 (unsigned int)vcu_buffer->size,
				 (unsigned long)vcu_buffer->mem_priv,
				 atomic_read(&vcu_buffer->ref_cnt));
		}
	}

	if (vcu_queue->num_sec_handles != 0) {
		for (handle = 0; handle < vcu_queue->num_sec_handles; handle++) {
			vcu_sec_handle = &vcu_queue->sec_handles[handle];
			if (vcu_sec_handle->dbuf != NULL)
				dma_heap_buffer_free(vcu_sec_handle->dbuf);

			pr_debug("Free sec %d dbuf = %p sec_handle = %lx size = %d mem_priv = %lx ref_cnt = %d\n",
				handle, vcu_sec_handle->dbuf,
				(unsigned long)vcu_sec_handle->sec_handle,
				(unsigned int)vcu_sec_handle->size,
				(unsigned long)vcu_sec_handle->mem_priv,
				atomic_read(&vcu_sec_handle->ref_cnt));
		}
	}

	list_for_each_safe(p, q, &vcu_queue->pa_pages.list) {
		tmp = list_entry(p, struct vcu_pa_pages, list);
		cmdq_mbox_buf_free(
			vcu_queue->cmdq_clt,
			(void *)(unsigned long)tmp->kva,
			(dma_addr_t)tmp->pa);
		pr_info("Free cmdq pa %lx ref_cnt = %d\n", tmp->pa,
			atomic_read(&tmp->ref_cnt));
		list_del(p);
		kfree(tmp);
	}
	mutex_unlock(&vcu_queue->mmap_lock);
	kvfree(vcu_queue);
	vcu_queue = NULL;
}

void *mtk_vcu_set_buffer(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data, struct vb2_buffer *src_vb,
	struct vb2_buffer *dst_vb)
{
	struct mtk_vcu_mem *vcu_buffer;
	unsigned int num_buffers, plane;
	unsigned int buffer;
	dma_addr_t *dma_addr = NULL;
	struct dma_buf *dbuf = NULL;

	mutex_lock(&vcu_queue->mmap_lock);
	pr_debug("[%s] %d iova = %llx src_vb = %p dst_vb = %p\n",
		__func__, vcu_queue->num_buffers, mem_buff_data->iova,
		src_vb, dst_vb);

	num_buffers = vcu_queue->num_buffers;
	if (mem_buff_data->len > CODEC_ALLOCATE_MAX_BUFFER_SIZE ||
		mem_buff_data->len == 0U || num_buffers >= CODEC_MAX_BUFFER) {
		pr_info("Set buffer fail: buffer len = %u num_buffers = %d !!\n",
			mem_buff_data->len, num_buffers);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	for (buffer = 0; buffer < num_buffers; buffer++) {
		vcu_buffer = &vcu_queue->bufs[buffer];
		if (mem_buff_data->iova == (u64)vcu_buffer->iova) {
			atomic_inc(&vcu_buffer->ref_cnt);
			mutex_unlock(&vcu_queue->mmap_lock);
			return vcu_buffer->mem_priv;
		}
	}

	vcu_buffer = &vcu_queue->bufs[num_buffers];
	if (dbuf == NULL && src_vb != NULL)
		for (plane = 0; plane < src_vb->num_planes; plane++) {
			dma_addr = src_vb->vb2_queue->mem_ops->cookie(src_vb,
				src_vb->planes[plane].mem_priv);
			if (*dma_addr == mem_buff_data->iova) {
				dbuf = src_vb->planes[plane].dbuf;
				vcu_buffer->size = src_vb->planes[plane].length;
				vcu_buffer->mem_priv = src_vb->planes[plane].mem_priv;
				//op = DMA_TO_DEVICE;
				pr_debug("src size = %d mem_buff_data len = %d\n",
					(unsigned int)vcu_buffer->size,
					(unsigned int)mem_buff_data->len);
			}
		}
	if (dbuf == NULL && dst_vb != NULL)
		for (plane = 0; plane < dst_vb->num_planes; plane++) {
			dma_addr = dst_vb->vb2_queue->mem_ops->cookie(dst_vb,
				dst_vb->planes[plane].mem_priv);
			if (*dma_addr == mem_buff_data->iova) {
				dbuf = dst_vb->planes[plane].dbuf;
				vcu_buffer->size = dst_vb->planes[plane].length;
				vcu_buffer->mem_priv = dst_vb->planes[plane].mem_priv;
				//op = DMA_FROM_DEVICE;
				pr_debug("dst size = %d mem_buff_data len = %d\n",
					(unsigned int)vcu_buffer->size,
					(unsigned int)mem_buff_data->len);
			}
		}

	if (dbuf == NULL) {
		mutex_unlock(&vcu_queue->mmap_lock);
		pr_debug("Set buffer not found: buffer len = %u iova = %llx !!\n",
			mem_buff_data->len, mem_buff_data->iova);
		return ERR_PTR(-ENOMEM);
	}
	vcu_buffer->dbuf = dbuf;
	vcu_buffer->iova = *dma_addr;
	get_file(dbuf->file);
	vcu_queue->num_buffers++;
	atomic_set(&vcu_buffer->ref_cnt, 1);
	mutex_unlock(&vcu_queue->mmap_lock);

	pr_debug("[%s] Num_buffers = %d iova = %llx dbuf = %p size = %d mem_priv = %lx\n",
		__func__, vcu_queue->num_buffers, mem_buff_data->iova,
		vcu_buffer->dbuf, (unsigned int)vcu_buffer->size,
		(unsigned long)vcu_buffer->mem_priv);

	return vcu_buffer->mem_priv;
}

void *mtk_vcu_get_buffer(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	void *cook, *dma_addr;
	struct mtk_vcu_mem *vcu_buffer;
	unsigned int buffers;

	mutex_lock(&vcu_queue->mmap_lock);
	buffers = vcu_queue->num_buffers;
	if (mem_buff_data->len > CODEC_ALLOCATE_MAX_BUFFER_SIZE ||
		mem_buff_data->len == 0U || buffers >= CODEC_MAX_BUFFER) {
		pr_info("Get buffer fail: buffer len = %u num_buffers = %d !!\n",
			mem_buff_data->len, buffers);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	vcu_buffer = &vcu_queue->bufs[buffers];
	vcu_buffer->vb.vb2_queue = &vcu_queue->vb_queue;
	vcu_buffer->mem_priv = vcu_queue->mem_ops->alloc(&vcu_buffer->vb, vcu_queue->dev,
		mem_buff_data->len);
	vcu_buffer->size = mem_buff_data->len;
	vcu_buffer->dbuf = NULL;
	if (IS_ERR_OR_NULL(vcu_buffer->mem_priv)) {
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-ENOMEM);
	}

	cook = vcu_queue->mem_ops->vaddr(&vcu_buffer->vb, vcu_buffer->mem_priv);
	dma_addr = vcu_queue->mem_ops->cookie(&vcu_buffer->vb, vcu_buffer->mem_priv);

	mem_buff_data->iova = *(dma_addr_t *)dma_addr;
	vcu_buffer->iova = *(dma_addr_t *)dma_addr;
	mtk_vcu_va_cnt++;
	if (mtk_vcu_va_cnt == 0)
		mtk_vcu_va_cnt++;
	vcu_buffer->va_id = mtk_vcu_va_cnt;
	mem_buff_data->va = vcu_buffer->va_id;
	mem_buff_data->pa = 0;
	vcu_queue->num_buffers++;
	mutex_unlock(&vcu_queue->mmap_lock);
	atomic_set(&vcu_buffer->ref_cnt, 1);

	pr_debug("[%s] Num_buffers = %d iova = %llx va = %lx va_id = %lld size = %d mem_priv = %lx\n",
		__func__, vcu_queue->num_buffers, mem_buff_data->iova,
		(unsigned long)cook, vcu_buffer->va_id, (unsigned int)vcu_buffer->size,
		(unsigned long)vcu_buffer->mem_priv);

	return vcu_buffer->mem_priv;
}

void *mtk_vcu_get_sec_handle(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	struct mtk_vcu_sec_handle *vcu_sec_handle;
	unsigned int handles;
	struct dma_heap *dma_heap;
	uint32_t sec_handle;

	mutex_lock(&vcu_queue->mmap_lock);
	handles = vcu_queue->num_sec_handles;
	if (mem_buff_data->len == 0U || handles >= CODEC_MAX_BUFFER) {
		pr_info("Get sec handle fail: buffer len = %u num_sec_handles = %d !!\n",
			mem_buff_data->len, handles);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	vcu_sec_handle = &vcu_queue->sec_handles[handles];
	dma_heap = dma_heap_find("mtk_wfd_region-aligned");
	if (!dma_heap) {
		pr_info("[%s] dma heap find fail\n", __func__);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	vcu_sec_handle->dbuf = dma_heap_buffer_alloc(dma_heap, mem_buff_data->len,
		O_RDWR | O_CLOEXEC, DMA_HEAP_VALID_HEAP_FLAGS);
	if (IS_ERR(vcu_sec_handle->dbuf)) {
		pr_info("[%s] dma heap buffer alloc fail\n", __func__);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	sec_handle = dmabuf_to_secure_handle(vcu_sec_handle->dbuf);
	mem_buff_data->va = 0;
	mem_buff_data->iova = 0;
	mem_buff_data->pa = sec_handle;
	vcu_sec_handle->mem_priv = vcu_sec_handle->dbuf;
	vcu_sec_handle->size = mem_buff_data->len;
	vcu_sec_handle->sec_handle = sec_handle;

	if (IS_ERR_OR_NULL(vcu_sec_handle->mem_priv)) {
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-ENOMEM);
	}

	vcu_queue->num_sec_handles++;
	mutex_unlock(&vcu_queue->mmap_lock);
	atomic_set(&vcu_sec_handle->ref_cnt, 1);

	pr_debug("[%s] Num_buffers = %d sec_handle = %llx size = %d mem_priv = %lx\n",
		__func__, vcu_queue->num_sec_handles, mem_buff_data->pa,
		(unsigned int)vcu_sec_handle->size,
		(unsigned long)vcu_sec_handle->mem_priv);

	return vcu_sec_handle->mem_priv;
}


void *mtk_vcu_get_sec_buffer(struct device *dev, struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	struct mtk_vcu_mem *vcu_sec_buf;
	unsigned int num_sec_buf = 0;
	struct dma_heap *dma_heap;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;

	mutex_lock(&vcu_queue->mmap_lock);
	num_sec_buf = vcu_queue->num_sec_buffers;
	if (mem_buff_data->len == 0U || num_sec_buf >= CODEC_MAX_BUFFER) {
		pr_info("Get sec handle fail: buffer len = %u num_sec_handles = %d !!\n",
			mem_buff_data->len, num_sec_buf);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	vcu_sec_buf = &vcu_queue->sec_bufs[num_sec_buf];
	dma_heap = dma_heap_find("mtk_wfd_region-aligned");
	if (!dma_heap) {
		pr_info("[%s] dma heap find fail\n", __func__);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	vcu_sec_buf->dbuf = dma_heap_buffer_alloc(dma_heap, mem_buff_data->len,
		O_RDWR | O_CLOEXEC, DMA_HEAP_VALID_HEAP_FLAGS);

	dma_heap_put(dma_heap);
	if (IS_ERR(vcu_sec_buf->dbuf)) {
		pr_info("[%s] dma heap buffer alloc fail\n", __func__);
		mutex_unlock(&vcu_queue->mmap_lock);
		return ERR_PTR(-EINVAL);
	}

	attach = dma_buf_attach(vcu_sec_buf->dbuf, dev);
	if (IS_ERR(attach)) {
		pr_info("[%s] attach fail, return\n", __func__);
		return NULL;
	}
	vcu_sec_buf->attach = attach;

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		pr_info("map failed, detach and return\n");
		dma_buf_detach(vcu_sec_buf->dbuf, attach);
		return NULL;
	}
	vcu_sec_buf->sgt = sgt;
	mem_buff_data->iova = sg_dma_address(sgt->sgl);
	mem_buff_data->va = 0;
	mem_buff_data->pa = 0;
	vcu_sec_buf->mem_priv = vcu_sec_buf->dbuf;
	vcu_sec_buf->size = mem_buff_data->len;
	vcu_sec_buf->iova = mem_buff_data->iova;


	vcu_queue->num_sec_buffers++;
	mutex_unlock(&vcu_queue->mmap_lock);
	atomic_set(&vcu_sec_buf->ref_cnt, 1);

	pr_info("[%s] Num_buffers = %d sec_iova = %llx size = %d mem_priv = %lx\n",
		__func__, vcu_queue->num_sec_buffers, mem_buff_data->iova,
		(unsigned int)vcu_sec_buf->size,
		(unsigned long)vcu_sec_buf->mem_priv);

	return vcu_sec_buf->mem_priv;
}

void *mtk_vcu_get_page(struct mtk_vcu_queue *vcu_queue,
						 struct mem_obj *mem_buff_data)
{
	dma_addr_t temp_pa = 0;
	void *mem_priv;
	struct vcu_pa_pages *tmp;

	mem_priv = cmdq_mbox_buf_alloc(vcu_queue->cmdq_clt, &temp_pa);
	tmp = kmalloc(sizeof(struct vcu_pa_pages), GFP_KERNEL);
	if (!tmp)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&vcu_queue->mmap_lock);
	tmp->pa = temp_pa;
	mem_buff_data->pa = temp_pa;
	tmp->kva = (unsigned long)mem_priv;
	mem_buff_data->va = CODEC_MSK((unsigned long)mem_priv);
	mem_buff_data->iova = 0;
	atomic_set(&tmp->ref_cnt, 1);
	list_add_tail(&tmp->list, &vcu_queue->pa_pages.list);
	mutex_unlock(&vcu_queue->mmap_lock);

	return mem_priv;
}

int mtk_vcu_free_buffer(struct mtk_vcu_queue *vcu_queue,
						struct mem_obj *mem_buff_data)
{
	struct mtk_vcu_mem *vcu_buffer;
	void *cook, *dma_addr;
	unsigned int buffer, num_buffers;
	int ret = -EINVAL;

	mutex_lock(&vcu_queue->mmap_lock);
	num_buffers = vcu_queue->num_buffers;
	if (num_buffers != 0U) {
		for (buffer = 0; buffer < num_buffers; buffer++) {
			vcu_buffer = &vcu_queue->bufs[buffer];
			if (vcu_buffer->dbuf != NULL)
				continue;
			if (vcu_buffer->mem_priv == NULL || vcu_buffer->size == 0) {
				pr_info("[VCU][Error] %s remove invalid vcu_queue bufs[%u] in num_buffers %u (mem_priv 0x%lx size %zu ref_cnt %d)\n",
					__func__, buffer, num_buffers,
					(unsigned long)vcu_buffer->mem_priv, vcu_buffer->size,
					atomic_read(&vcu_buffer->ref_cnt));
				vcu_buf_remove(vcu_queue, buffer);
				continue;
			}
			cook = vcu_queue->mem_ops->vaddr(&vcu_buffer->vb, vcu_buffer->mem_priv);
			dma_addr = vcu_queue->mem_ops->cookie(&vcu_buffer->vb, vcu_buffer->mem_priv);

			if (mem_buff_data->va == vcu_buffer->va_id &&
			    mem_buff_data->iova == *(dma_addr_t *)dma_addr &&
			    mem_buff_data->len == vcu_buffer->size &&
			    atomic_read(&vcu_buffer->ref_cnt) == 1) {
				pr_debug("Free buff = %d iova = %llx va = %lx va_id = %llx, queue_num = %d\n",
					buffer, mem_buff_data->iova,
					(unsigned long)cook, mem_buff_data->va,
					num_buffers);
				vcu_queue->mem_ops->put(vcu_buffer->mem_priv);
				atomic_dec(&vcu_buffer->ref_cnt);
				vcu_buf_remove(vcu_queue, buffer);
				ret = 0;
				break;
			}
		}
	}
	mutex_unlock(&vcu_queue->mmap_lock);

	if (ret != 0)
		pr_info("Can not free memory va %llx iova %llx len %u!\n",
			mem_buff_data->va, mem_buff_data->iova,
			mem_buff_data->len);

	return ret;
}

int mtk_vcu_free_sec_handle(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	struct mtk_vcu_sec_handle *vcu_sec_handle;
	unsigned int handle, num_sec_handles, last_sec_handles;
	int ret = -EINVAL;

	mutex_lock(&vcu_queue->mmap_lock);
	num_sec_handles = vcu_queue->num_sec_handles;
	if (num_sec_handles != 0U) {
		for (handle = 0; handle < num_sec_handles; handle++) {
			vcu_sec_handle = &vcu_queue->sec_handles[handle];
			if (mem_buff_data->pa == vcu_sec_handle->sec_handle &&
				mem_buff_data->len == vcu_sec_handle->size &&
				atomic_read(&vcu_sec_handle->ref_cnt) == 1) {
				pr_debug("Free handle = %d sec_handle = %llx, queue_num = %d\n",
					handle, mem_buff_data->pa, num_sec_handles);

				/* decrease file count */
				dma_heap_buffer_free(vcu_sec_handle->dbuf);
				atomic_dec(&vcu_sec_handle->ref_cnt);

				last_sec_handles = num_sec_handles - 1U;
				if (last_sec_handles != handle)
					vcu_queue->sec_handles[handle] =
						vcu_queue->sec_handles[last_sec_handles];
				vcu_queue->sec_handles[last_sec_handles].mem_priv = NULL;
				vcu_queue->sec_handles[last_sec_handles].size = 0;
				vcu_queue->sec_handles[last_sec_handles].dbuf = NULL;
				vcu_queue->sec_handles[last_sec_handles].sec_handle = 0;
				vcu_queue->num_sec_handles--;
				ret = 0;
				break;
			}
		}
	}
	mutex_unlock(&vcu_queue->mmap_lock);

	if (ret != 0)
		pr_info("Can not free memory sec_handles %llx len %u!\n",
			mem_buff_data->pa, mem_buff_data->len);

	return ret;
}

int mtk_vcu_free_sec_buffer(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	struct mtk_vcu_mem *vcu_sec_buf;
	unsigned int i, last_sec_buf;
	unsigned int num_sec_buf = 0;
	int ret = -EINVAL;

	mutex_lock(&vcu_queue->mmap_lock);
	num_sec_buf = vcu_queue->num_sec_buffers;

	pr_info("Free buffer sec_iova = %llx, len %d queue_num = %d\n",
		mem_buff_data->iova, mem_buff_data->len, num_sec_buf);
	if (num_sec_buf != 0U) {
		for (i = 0; i < num_sec_buf; i++) {
			vcu_sec_buf = &vcu_queue->sec_bufs[i];
			if (mem_buff_data->iova == vcu_sec_buf->iova &&
				mem_buff_data->len == vcu_sec_buf->size &&
				atomic_read(&vcu_sec_buf->ref_cnt) == 1) {
				pr_info("Free buffer index = %d sec_iova = %llx, queue_num = %d\n",
					i, mem_buff_data->iova, num_sec_buf);

				/* decrease file count */
				//free iova here
				dma_buf_unmap_attachment(vcu_sec_buf->attach,
					vcu_sec_buf->sgt,
					DMA_BIDIRECTIONAL);

				dma_buf_detach(vcu_sec_buf->dbuf, vcu_sec_buf->attach);
				//free buffer here
				dma_heap_buffer_free(vcu_sec_buf->dbuf);

				atomic_dec(&vcu_sec_buf->ref_cnt);

				last_sec_buf = num_sec_buf - 1U;
				if (last_sec_buf != i)
					vcu_queue->sec_bufs[i] =
						vcu_queue->sec_bufs[last_sec_buf];
				vcu_queue->sec_bufs[last_sec_buf].mem_priv = NULL;
				vcu_queue->sec_bufs[last_sec_buf].size = 0;
				vcu_queue->sec_bufs[last_sec_buf].dbuf = NULL;
				vcu_queue->sec_bufs[last_sec_buf].iova = 0;
				vcu_queue->sec_bufs[last_sec_buf].attach = NULL;
				vcu_queue->sec_bufs[last_sec_buf].sgt = NULL;

				vcu_queue->num_sec_buffers--;
				ret = 0;
				break;
			}
		}
	}
	mutex_unlock(&vcu_queue->mmap_lock);

	if (ret != 0)
		pr_info("Can not free memory sec_iova %llx len %u!\n",
			mem_buff_data->iova, mem_buff_data->len);

	return ret;
}

int mtk_vcu_free_page(struct mtk_vcu_queue *vcu_queue,
	struct mem_obj *mem_buff_data)
{
	int ret = -EINVAL;
	struct vcu_pa_pages *tmp;
	struct list_head *p, *q;

	mutex_lock(&vcu_queue->mmap_lock);
	list_for_each_safe(p, q, &vcu_queue->pa_pages.list) {
		tmp = list_entry(p, struct vcu_pa_pages, list);
		if (tmp->pa == mem_buff_data->pa &&
			CODEC_MSK(tmp->kva) == mem_buff_data->va &&
			atomic_read(&tmp->ref_cnt) == 1) {
			ret = 0;
			cmdq_mbox_buf_free(
				vcu_queue->cmdq_clt,
				(void *)(unsigned long)
				tmp->kva,
				(dma_addr_t)mem_buff_data->pa);
			atomic_dec(&tmp->ref_cnt);
			list_del(p);
			kfree(tmp);
			break;
		}
	}
	mutex_unlock(&vcu_queue->mmap_lock);

	if (ret != 0)
		pr_info("Can not free memory va %llx pa %llx len %u!\n",
			mem_buff_data->va, mem_buff_data->pa,
			mem_buff_data->len);

	return ret;
}

void mtk_vcu_buffer_ref_dec(struct mtk_vcu_queue *vcu_queue,
	void *mem_priv)
{
	struct mtk_vcu_mem *vcu_buffer;
	unsigned int buffer, num_buffers;

	mutex_lock(&vcu_queue->mmap_lock);
	num_buffers = vcu_queue->num_buffers;
	for (buffer = 0; buffer < num_buffers; buffer++) {
		vcu_buffer = &vcu_queue->bufs[buffer];
		if (vcu_buffer->mem_priv == mem_priv) {
			if (atomic_read(&vcu_buffer->ref_cnt) > 0)
				atomic_dec(&vcu_buffer->ref_cnt);
			else
				pr_info("[VCU][Error] %s fail\n", __func__);

			if (atomic_read(&vcu_buffer->ref_cnt) == 0
				&& vcu_buffer->dbuf != NULL) {
				pr_debug("Free IO buff = %d iova = %pad mem_priv = %llx, queue_num = %d\n",
					buffer, &vcu_buffer->iova,
					(unsigned long long)vcu_buffer->mem_priv, num_buffers);
				fput(vcu_buffer->dbuf->file);
				vcu_buf_remove(vcu_queue, buffer);
			}
		}
	}
	mutex_unlock(&vcu_queue->mmap_lock);
}

void vcu_io_buffer_cache_sync(struct device *dev,
	struct dma_buf *dbuf, int op)
{
	struct dma_buf_attachment *buf_att;
	struct sg_table *sgt;

	buf_att = dma_buf_attach(dbuf, dev);
	if (IS_ERR(buf_att)) {
		pr_info("failed to attach dmabuf\n");
		return;
	}
	sgt = dma_buf_map_attachment_unlocked(buf_att, op);
	if (IS_ERR_OR_NULL(sgt)) {
		pr_info("%s Error getting dmabuf scatterlist %lx\n", __func__, (unsigned long)sgt);
		dma_buf_detach(dbuf, buf_att);
		return;
	}
	if (op == DMA_FROM_DEVICE)
		dma_sync_sg_for_cpu(dev, sgt->sgl, sgt->orig_nents, op);
	else
		dma_sync_sg_for_device(dev, sgt->sgl, sgt->orig_nents, op);
	dma_buf_unmap_attachment_unlocked(buf_att, sgt, op);
	dma_buf_detach(dbuf, buf_att);
}

int vcu_buffer_flush_all(struct device *dev, struct mtk_vcu_queue *vcu_queue)
{
	struct mtk_vcu_mem *vcu_buffer;
	unsigned int buffer, num_buffers;
	struct dma_buf *dbuf = NULL;
	unsigned long flags = 0;

	dev_dbg(dev, "[VCU] Flush cache in kernel\n");
	mutex_lock(&vcu_queue->mmap_lock);

	num_buffers = vcu_queue->num_buffers;
	if (num_buffers == 0U) {
		mutex_unlock(&vcu_queue->mmap_lock);
		return 0;
	}

	for (buffer = 0; buffer < num_buffers; buffer++) {
		vcu_buffer = &vcu_queue->bufs[buffer];
		pr_debug("Cache clean %s buffer=%d iova=%lx size=%d num=%d\n",
			(vcu_buffer->dbuf == NULL) ? "working" : "io",
			buffer, (unsigned int long)vcu_buffer->iova,
			(unsigned int)vcu_buffer->size, num_buffers);

		if (vcu_buffer->dbuf == NULL)
			dbuf = vcu_queue->mem_ops->get_dmabuf(&vcu_buffer->vb, vcu_buffer->mem_priv, flags);
		else
			dbuf = vcu_buffer->dbuf;

		vcu_io_buffer_cache_sync(dev, dbuf, DMA_TO_DEVICE);

		if (vcu_buffer->dbuf == NULL)
			dma_buf_put(dbuf);
	}

	mutex_unlock(&vcu_queue->mmap_lock);

	return 0;
}

int vcu_buffer_cache_sync(struct device *dev, struct mtk_vcu_queue *vcu_queue,
	dma_addr_t dma_addr, size_t size, int op)
{
	struct mtk_vcu_mem *vcu_buffer;
	unsigned int num_buffers = 0;
	unsigned int buffer = 0;
	struct dma_buf *dbuf = NULL;
	unsigned long flags = 0;

	mutex_lock(&vcu_queue->mmap_lock);

	num_buffers = vcu_queue->num_buffers;
	if (num_buffers == 0U) {
		pr_info("Cache %s buffer fail, iova = %lx, size = %d, vcu no buffers\n",
			(op == DMA_TO_DEVICE) ? "flush" : "invalidate",
			(unsigned long)dma_addr, (unsigned int)size);
		mutex_unlock(&vcu_queue->mmap_lock);
		return -1;
	}

	for (buffer = 0; buffer < num_buffers; buffer++) {
		vcu_buffer = &vcu_queue->bufs[buffer];
		if ((dma_addr + size) <=
			(vcu_buffer->iova + vcu_buffer->size) &&
			dma_addr >= vcu_buffer->iova) {
			pr_debug("Cache %s %s buffer iova=%lx range=%d (%lx %d)\n",
				(op == DMA_TO_DEVICE) ?
				"clean" : "invalidate",
				(vcu_buffer->dbuf == NULL) ?
				"working" : "io",
				(unsigned long)dma_addr, (unsigned int)size,
				(unsigned long)vcu_buffer->iova,
				(unsigned int)vcu_buffer->size);

			if (vcu_buffer->dbuf == NULL)
				dbuf = vcu_queue->mem_ops->get_dmabuf(&vcu_buffer->vb, vcu_buffer->mem_priv, flags);
			else
				dbuf = vcu_buffer->dbuf;

			vcu_io_buffer_cache_sync(dev, dbuf, op);

			if (vcu_buffer->dbuf == NULL)
				dma_buf_put(dbuf);

			mutex_unlock(&vcu_queue->mmap_lock);
			return 0;
		}
	}

	pr_info("Cache %s buffer fail, iova = %lx, size = %d\n",
		(op == DMA_TO_DEVICE) ? "flush" : "invalidate",
		(unsigned long)dma_addr, (unsigned int)size);
	mutex_unlock(&vcu_queue->mmap_lock);

	return -1;
}

static int vcu_dmabuf_get_dma_addr(struct device *dev,
					   struct mtk_vcu_queue *vcu_queue,
					   int share_fd, dma_addr_t *dma_addr, int *is_sec,
					   int add_fcnt)
{
	struct dma_buf *buf = dma_buf_get(share_fd);		/* f_cnt +1 */
	struct dma_buf_attachment *attach;
	struct sg_table *table;
	int ret = 0, sec = 0;
	const char *reason;

	if (IS_ERR(buf)) {
		reason = "buf_get";
		ret = (int)PTR_ERR(buf);
		goto out;
	}
	attach = dma_buf_attach(buf, dev);
	if (IS_ERR(attach)) {
		reason = "buf_attach";
		ret = (int)PTR_ERR(attach);
		goto put_buf;
	}
	table = dma_buf_map_attachment_unlocked(attach, 0);
	if (IS_ERR(table)) {
		reason = "buf_map";
		ret = (int)PTR_ERR(table);
		goto detach_buf;
	}

	*dma_addr = sg_dma_address(table->sgl);
	sec = sg_page(table->sgl) ? 0 : 1;
	if (is_sec)
		*is_sec = sec;

	if (add_fcnt) {
		pr_info("%s: %s, share_id %d, f_cnt +1\n", __func__,
			 dev_name(dev), share_fd);
		get_dma_buf(buf);
	}

	dma_buf_unmap_attachment_unlocked(attach, table, 0);
detach_buf:
	dma_buf_detach(buf, attach);
put_buf:
	dma_buf_put(buf);

out:
	if (ret)
		pr_info("%s: %s fail(%s:%d) share_id %d\n", __func__,
		       dev_name(dev), reason, ret, share_fd);
	else
		pr_debug("%s: %s, share_id %d, dma_addr 0x%lx(sec %d, add_cnt %d)\n",
			 __func__, dev_name(dev),
			 share_fd, (unsigned long)(*dma_addr),
			 sec, add_fcnt);
	return ret;
}

int mtk_vcu_get_dma_addr(struct device *dev, struct mtk_vcu_queue *vcu_queue,
				   int share_fd, dma_addr_t *dma_addr, int *is_sec)
{
	if (!dma_addr)
		return -EINVAL;
	return vcu_dmabuf_get_dma_addr(dev, vcu_queue, share_fd, dma_addr, is_sec,
					       0);
}

MODULE_IMPORT_NS(DMA_BUF);
