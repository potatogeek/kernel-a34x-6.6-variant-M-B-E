load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load(
    "//build/kernel/kleaf:constants.bzl",
    "DEFAULT_GKI_OUTS",
)
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_abi",
    "kernel_abi_dist",
    "kernel_build",
    "kernel_module",
    "kernel_modules_install",
)
load(
    "//build/bazel_common_rules/dist:dist.bzl",
    "copy_to_dist_dir",
)
load("@mgk_info//:dict.bzl",
    "DEFCONFIG_OVERLAYS",
)

kernel_versions_and_projects = {
   "6.1": "mgk_64_k61 mgk_64_aging_k61 mgk_64_entry_level_k61 mgk_64_fpga_k61 mgk_64_k61_thinmodem mgk_64_k61_wifi mgk_64_kasan_k61 mgk_64_khwasan_k61 mgk_64_pkvm_k61 mgk_64_vulscan_k61",
   "6.6": "mgk_64_k66",
   "mainline": "mgk_64_kmainline",
}

def get_real_modules_list(common_modules, platform_modules):
    real_modules = []
    for k in common_modules:
        file_path = paths.dirname(k) + "/*"
        if native.glob([file_path]):
            real_modules.append(k)

    mgk_platforms = [paths.dirname(p) for p in native.glob(["*/mgk.enabled"])]
    for k,v in platform_modules.items():
        for plat in v.split(" "):
            if (plat in mgk_platforms) and (k not in real_modules):
                file_path = paths.dirname(k) + "/*"
                if native.glob([file_path]):
                    real_modules.append(k)
    return real_modules

def get_kleaf_modules_list(kleaf_modules):
    kleaf_switch = {}
    for m in kleaf_modules:
        p = m.partition(":")[2]
        if p.endswith("_cus"):
            k = p[:-4]
            kleaf_switch[k] = m
    kleaf_internal = []
    kleaf_customer = []
    kleaf_msync2_customer = 0
    for m in kleaf_modules:
        p = m.partition(":")[2]
        is_cus = 0
        if p in kleaf_switch:
            is_cus = -1
        else:
            if m.startswith("//vendor/mediatek/kernel_modules/msync2_frd_cus"):
                kleaf_msync2_customer = 1
                continue
            elif m.startswith("//vendor/mediatek/kernel_modules/cpufreq_"):
                is_cus = -1
            elif p.endswith("_cus"):
                is_cus = 1
            elif p.endswith("_int"):
                k = p[:-4]
                if k in kleaf_switch:
                    is_cus = -1
            elif p.startswith("met_drv_secure"):
                is_cus = -1
            elif m.startswith("//vendor/mediatek/kernel_modules/mtk_input/"):
                is_cus = -1
            elif m.startswith("//vendor/mediatek/tests/"):
                is_cus = -1
        if is_cus == 0:
            kleaf_internal.append(m)
            kleaf_customer.append(m)
        elif is_cus == 1:
            kleaf_customer.append(m)
        elif is_cus == -1:
            kleaf_internal.append(m)
    return kleaf_internal, kleaf_customer, kleaf_msync2_customer

def define_mgk(
        name,
        defconfig,
        kleaf_modules,
        kleaf_eng_modules,
        kleaf_userdebug_modules,
        kleaf_user_modules,
        common_modules,
        common_eng_modules,
        common_userdebug_modules,
        common_user_modules,
        device_modules,
        platform_device_modules,
        device_eng_modules,
        platform_device_eng_modules,
        device_userdebug_modules,
        platform_device_userdebug_modules,
        device_user_modules,
        platform_device_user_modules,
        symbol_list):
    mgk_defconfig_overlays = []
    for o in DEFCONFIG_OVERLAYS.split(" "):
        if o != "":
            mgk_defconfig_overlays.append(o)

    mgk_defconfig = defconfig

    native.filegroup(
        name = "{}_sources".format(name),
        srcs = native.glob(
            ["**"],
            exclude = [
                ".*",
                ".*/**",
                "BUILD.bazel",
                "**/*.bzl",
                "build.config.*",
            ],
        ),
    )

    # get kleaf modules for internal and customer
    kleaf_internal, kleaf_customer, kleaf_msync2_customer = get_kleaf_modules_list(kleaf_modules)
    kleaf_eng_internal, kleaf_eng_customer, kleaf_eng_msync2_customer = get_kleaf_modules_list(kleaf_eng_modules)
    kleaf_userdebug_internal, kleaf_userdebug_customer, kleaf_userdebug_msync2_customer = get_kleaf_modules_list(kleaf_userdebug_modules)
    kleaf_user_internal, kleaf_user_customer, kleaf_user_msync2_customer = get_kleaf_modules_list(kleaf_user_modules)

    # deal with device modules list
    real_device_modules = get_real_modules_list(device_modules, platform_device_modules)
    real_device_eng_modules = get_real_modules_list(device_eng_modules, platform_device_eng_modules)
    real_device_userdebug_modules = get_real_modules_list(device_userdebug_modules, platform_device_userdebug_modules)
    real_device_user_modules = get_real_modules_list(device_user_modules, platform_device_user_modules)

    for build in ["eng", "userdebug", "user", "ack"]:
        ack_build = build
        ack_dir = "kernel"
        if build == "ack":
            # "ack" build uses kernel/common-x.y (pure) instead of kernel/kernel-x.y (modified) repo as base_kernel
            ack_dir = "common"
        #elif build == "user":
            # "user" build applies Kconfig.ext and mgk_*_defconfig in base_kernel, especially disabling
            # CONFIG_MODULE_SIG_ALL, the result may be different from GKI.
        #    ack_build = "ack"
        if True:
            # for device module tree
            mgk_build_config(
                name = "{}_build_config.{}".format(name, build),
                kernel_dir = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : "{}-{}".format(ack_dir, "6.1"),
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : "{}-{}".format(ack_dir, "6.6"),
                    "//build/bazel_mgk_rules:kernel_version_mainline": "{}-{}".format(ack_dir, "mainline"),
                    "//conditions:default"                           : ack_dir,
                }),
                device_modules_dir = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : "kernel_device_modules-{}".format("6.1"),
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : "kernel_device_modules-{}".format("6.6"),
                    "//build/bazel_mgk_rules:kernel_version_mainline": "kernel_device_modules-{}".format("mainline"),
                    "//conditions:default"                           : "kernel_device_modules",
                }),
                defconfig = mgk_defconfig,
                defconfig_overlays = mgk_defconfig_overlays,
                build_config_overlays = [],
                build_variant = build,
                kleaf_modules = kleaf_modules + kleaf_eng_modules + kleaf_userdebug_modules + kleaf_user_modules,
                gki_mixed_build = True,
            )
        if build != "ack":
            # for android common kernel tree
            mgk_build_config(
                name = "kernel_aarch64_{}_build_config.{}".format(name, build),
                kernel_dir = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : "{}-{}".format(ack_dir, "6.1"),
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : "{}-{}".format(ack_dir, "6.6"),
                    "//build/bazel_mgk_rules:kernel_version_mainline": "{}-{}".format(ack_dir, "mainline"),
                    "//conditions:default"                           : ack_dir,
                }),
                device_modules_dir = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : "kernel_device_modules-{}".format("6.1"),
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : "kernel_device_modules-{}".format("6.6"),
                    "//build/bazel_mgk_rules:kernel_version_mainline": "kernel_device_modules-{}".format("mainline"),
                    "//conditions:default"                           : "kernel_device_modules",
                }),
                defconfig = mgk_defconfig,
                defconfig_overlays = mgk_defconfig_overlays,
                build_config_overlays = [],
                build_variant = build,
                kleaf_modules = kleaf_modules + kleaf_eng_modules + kleaf_userdebug_modules + kleaf_user_modules,
                gki_mixed_build = False,
            )
        if build != "ack":
            # for android common kernel tree
            kernel_build(
                name = "{}_kernel_aarch64.{}".format(name, build),
                srcs = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "6.1")],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "6.6")],
                    "//build/bazel_mgk_rules:kernel_version_mainline": ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "mainline")],
                    "//conditions:default"                           : ["//{}:kernel_aarch64_sources".format(ack_dir)],
                }) + [
                    ":mgk_configs",
                    ":{}_sources".format(name),
                ],
                build_config = ":kernel_aarch64_{}_build_config.{}".format(name, build),
                kconfig_ext = None if build == "ack" else ":Kconfig.ext",
                make_goals = [
                    "PAHOLE_FLAGS=--btf_gen_floats",
                    "Image",
                    "Image.lz4",
                    "Image.gz",
                    "modules",
                ],
                strip_modules=True,
                outs = DEFAULT_GKI_OUTS,
                module_outs = common_eng_modules if build == "eng" else common_userdebug_modules if build == "userdebug" else common_user_modules if build == "user" else [],
                module_implicit_outs = common_modules if build == "ack" else [],
                base_kernel = None,
                trim_nonlisted_kmi = False,
            )
        if True:
            # for device module tree
            kernel_build(
                name = "{}.{}".format(name, build),
                srcs = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "6.1")],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "6.6")],
                    "//build/bazel_mgk_rules:kernel_version_mainline": ["//{}-{}:kernel_aarch64_sources".format(ack_dir, "mainline")],
                    "//conditions:default"                           : ["//{}:kernel_aarch64_sources".format(ack_dir)],
                }) + [
                    ":{}_sources".format(name),
                ],
                outs = [
                    ".config",
                ],
                module_outs = common_eng_modules if build == "eng" else common_userdebug_modules if build == "userdebug" else [],
                module_implicit_outs = common_user_modules if build == "user" else [],
                build_config = ":{}_build_config.{}".format(name, build),
                kconfig_ext = ":Kconfig.ext",
                make_goals = [
                    "modules",
                ],
                strip_modules = False,
                base_kernel = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : "//{}-{}:kernel_aarch64_debug".format(ack_dir, "6.1"),
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : "//{}-{}:kernel_aarch64_debug".format(ack_dir, "6.6"),
                    "//build/bazel_mgk_rules:kernel_version_mainline": "//{}-{}:kernel_aarch64_debug".format(ack_dir, "mainline"),
                    "//conditions:default"                           : None,
                }) if build == "ack" else ":{}_kernel_aarch64.{}".format(name, build),
                module_signing_key = "certs/mtk_signing_key.pem",
                modules_prepare_force_generate_headers = True,
                # ABI
                kmi_symbol_list = symbol_list,
                trim_nonlisted_kmi = False,
                kmi_symbol_list_strict_mode = False,
                collect_unstripped_modules = True,
            )
        # internal
        kernel_abi(
            name = "{}.{}_internal_abi".format(name, build),
            kernel_modules = [
                ":{}_modules.{}".format(name, build),
            ] + select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_internal],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_internal],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_internal],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_internal],
            }),
            kernel_build = ":{}.{}".format(name, build),
            #abi_definition_xml = "android/abi_gki_aarch64.xml",
            abi_definition_stg = "android/abi_gki_aarch64.stg",
            kmi_symbol_list_add_only = True,
            kmi_enforced = True,
            module_grouping = False,
        )
        # customer
        kernel_abi(
            name = "{}.{}_customer_abi".format(name, build),
            kernel_modules = [
                ":{}_modules.{}".format(name, build),
            ] + select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_customer],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_customer],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_customer],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_customer],
            }),
            kernel_build = ":{}.{}".format(name, build),
            #abi_definition_xml = "android/abi_gki_aarch64.xml",
            abi_definition_stg = "android/abi_gki_aarch64.stg",
            kmi_symbol_list_add_only = True,
            kmi_enforced = True,
            module_grouping = False,
        )
        # internal
        kernel_modules_install(
            name = "{}_internal_modules_install.{}".format(name, build),
            kernel_modules = [
                ":{}_modules.{}".format(name, build),
            ] + select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_internal],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_internal],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_internal],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_internal],
            }) + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_eng_internal],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_eng_internal],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_eng_internal],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_eng_internal],
            }) if build == "eng" else [])
              + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_userdebug_internal],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_userdebug_internal],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_userdebug_internal],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_userdebug_internal],
            }) if build == "userdebug" else [])
              + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_user_internal],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_user_internal],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_user_internal],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_user_internal],
            }) if build == "user" else []),
            kernel_build = ":{}.{}".format(name, build),
        )
        if build == "ack":
            copy_to_dist_dir(
                name = "{}_internal_dist.{}".format(name, build),
                data = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : ["//{}-{}:kernel_aarch64_debug".format("common", "6.1")],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : ["//{}-{}:kernel_aarch64_debug".format("common", "6.6")],
                    "//build/bazel_mgk_rules:kernel_version_mainline": ["//{}-{}:kernel_aarch64_debug".format("common", "mainline")],
                    "//conditions:default"                           : [],
                }) + [
                    ":{}.{}".format(name, build),
                    ":{}_internal_modules_install.{}".format(name, build),
                ],
                flat = False,
            )
        else:
            copy_to_dist_dir(
                name = "{}_internal_dist.{}".format(name, build),
                data = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : [":{}_kernel_aarch64.{}".format(name, build)],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : [":{}_kernel_aarch64.{}".format(name, build)],
                    "//build/bazel_mgk_rules:kernel_version_mainline": [":{}_kernel_aarch64.{}".format(name, build)],
                    "//conditions:default"                           : ["//kernel:kernel_aarch64.{}".format(build)],
                }) + (select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                    "//build/bazel_mgk_rules:kernel_version_mainline": [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                    "//conditions:default"                           : ["//kernel:kernel_aarch64.{}/{}".format(build, m) for m in common_modules],
                }) if ack_build == "ack" else [])
                + [
                    ":{}.{}".format(name, build),
                    ":{}_internal_modules_install.{}".format(name, build),
                ] + ([":{}.{}/{}".format(name, build, m) for m in common_user_modules] if build == "user" else []),
                flat = False,
            )
        # customer
        kernel_modules_install(
            name = "{}_customer_modules_install.{}".format(name, build),
            kernel_modules = [
                ":{}_modules.{}".format(name, build),
            ] + select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_customer],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_customer],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_customer],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_customer],
            }) + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_eng_customer],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_eng_customer],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_eng_customer],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_eng_customer],
            }) if build == "eng" else [])
              + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_userdebug_customer],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_userdebug_customer],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_userdebug_customer],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_userdebug_customer],
            }) if build == "userdebug" else [])
              + (select({
                "//build/bazel_mgk_rules:kernel_version_6.1"     : ["{}.{}.{}.{}".format(m, name, "6.1", build) for m in kleaf_user_customer],
                "//build/bazel_mgk_rules:kernel_version_6.6"     : ["{}.{}.{}.{}".format(m, name, "6.6", build) for m in kleaf_user_customer],
                "//build/bazel_mgk_rules:kernel_version_mainline": ["{}.{}.{}.{}".format(m, name, "mainline", build) for m in kleaf_user_customer],
                "//conditions:default"                           : ["{}.{}".format(m, build) for m in kleaf_user_customer],
            }) if build == "user" else [])
              + (select({
                "@mgk_ko//:msync2_lic_6.1_set": ["//vendor/mediatek/kernel_modules/msync2_frd_cus/build:msync2_frd_cus.{}.{}.{}".format(name, "6.1", build)],
                "@mgk_ko//:msync2_lic_6.6_set": ["//vendor/mediatek/kernel_modules/msync2_frd_cus/build:msync2_frd_cus.{}.{}.{}".format(name, "6.6", build)],
                "@mgk_ko//:msync2_lic_mainline_set": ["//vendor/mediatek/kernel_modules/msync2_frd_cus/build:msync2_frd_cus.{}.{}.{}".format(name, "mainline", build)],
                "//conditions:default": [],
            }) if kleaf_msync2_customer == 1 else []),
            kernel_build = ":{}.{}".format(name, build),
        )
        if build == "ack":
            copy_to_dist_dir(
                name = "{}_customer_dist.{}".format(name, build),
                data = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : ["//{}-{}:kernel_aarch64_debug".format("common", "6.1")],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : ["//{}-{}:kernel_aarch64_debug".format("common", "6.6")],
                    "//build/bazel_mgk_rules:kernel_version_mainline": ["//{}-{}:kernel_aarch64_debug".format("common", "mainline")],
                    "//conditions:default"                           : [],
                }) + [
                    ":{}.{}".format(name, build),
                    ":{}_customer_modules_install.{}".format(name, build),
                ],
                flat = False,
            )
        else:
            copy_to_dist_dir(
                name = "{}_customer_dist.{}".format(name, build),
                data = select({
                    "//build/bazel_mgk_rules:kernel_version_6.1"     : [":{}_kernel_aarch64.{}".format(name, build)],
                    "//build/bazel_mgk_rules:kernel_version_6.6"     : [":{}_kernel_aarch64.{}".format(name, build)],
                    "//build/bazel_mgk_rules:kernel_version_mainline": [":{}_kernel_aarch64.{}".format(name, build)],
                    "//conditions:default"                           : ["//kernel:kernel_aarch64.{}".format(build)],
                }) + (select({
                     "//build/bazel_mgk_rules:kernel_version_6.1"     : [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                     "//build/bazel_mgk_rules:kernel_version_6.6"     : [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                     "//build/bazel_mgk_rules:kernel_version_mainline": [":{}_kernel_aarch64.{}/{}".format(name, build, m) for m in common_modules],
                     "//conditions:default"                           : ["//kernel:kernel_aarch64.{}/{}".format(build, m) for m in common_modules],
                }) if ack_build == "ack" else [])
                + [
                    ":{}.{}".format(name, build),
                    ":{}_customer_modules_install.{}".format(name, build),
                ] + ([":{}.{}/{}".format(name, build, m) for m in common_user_modules] if build == "user" else []),
                flat = False,
            )

    kernel_module(
        name = "{}_modules.eng".format(name),
        srcs = [":{}_sources".format(name)],
        outs = real_device_modules + real_device_eng_modules,
        kernel_build = ":{}.eng".format(name),
    )
    kernel_module(
        name = "{}_modules.userdebug".format(name),
        srcs = [":{}_sources".format(name)],
        outs = real_device_modules + real_device_userdebug_modules,
        kernel_build = ":{}.userdebug".format(name),
    )
    kernel_module(
        name = "{}_modules.user".format(name),
        srcs = [":{}_sources".format(name)],
        outs = real_device_modules + real_device_user_modules,
        kernel_build = ":{}.user".format(name),
    )
    kernel_module(
        name = "{}_modules.ack".format(name),
        srcs = [":{}_sources".format(name)],
        outs = real_device_modules + real_device_user_modules,
        kernel_build = ":{}.ack".format(name),
    )


def _mgk_build_config_impl(ctx):
    content = []
    content.append("DEVICE_MODULES_DIR={}".format(ctx.attr.device_modules_dir))
    content.append("KERNEL_DIR={}".format(ctx.attr.kernel_dir))
    if ctx.attr.config_is_local[BuildSettingInfo].value:
        content.append("DEVICE_MODULES_REL_DIR=../kernel/${DEVICE_MODULES_DIR}")
    else:
        content.append("DEVICE_MODULES_REL_DIR=$(realpath ${DEVICE_MODULES_DIR} --relative-to ${KERNEL_DIR})")
    content.append("""
. ${ROOT_DIR}/${KERNEL_DIR}/build.config.common
. ${ROOT_DIR}/${KERNEL_DIR}/build.config.aarch64
. ${ROOT_DIR}/${KERNEL_DIR}/build.config.gki

DEVICE_MODULES_PATH="\\$(srctree)/\\$(DEVICE_MODULES_REL_DIR)"
DEVCIE_MODULES_INCLUDE="-I\\$(DEVICE_MODULES_PATH)/include"
""")
    if ctx.attr.gki_mixed_build or (ctx.attr.build_variant != "ack"):
        defconfig = []
        defconfig.append("${ROOT_DIR}/${KERNEL_DIR}/arch/arm64/configs/gki_defconfig")
        defconfig.append("${ROOT_DIR}/" + ctx.attr.device_modules_dir + "/arch/arm64/configs/${DEFCONFIG}")
        is_kasan_load = 0
        kasan_config = []
        if ctx.attr.defconfig_overlays:
            for overlay in ctx.attr.defconfig_overlays:
                overlay_config = "${ROOT_DIR}/" + ctx.attr.device_modules_dir + "/kernel/configs/" + overlay
                if overlay.startswith("kasan"):
                    is_kasan_load = 1
                    kasan_config.append(overlay_config)
                else:
                    defconfig.append(overlay_config)
        if ctx.attr.gki_mixed_build:
            defconfig.append("${ROOT_DIR}/" + ctx.attr.device_modules_dir + "/kernel/configs/sign.config")
        if ctx.attr.build_variant == "eng":
            defconfig.append("${ROOT_DIR}/" + ctx.attr.device_modules_dir + "/kernel/configs/eng.config")
        elif ctx.attr.build_variant == "userdebug":
            defconfig.append("${ROOT_DIR}/" + ctx.attr.device_modules_dir + "/kernel/configs/userdebug.config")
        if is_kasan_load == 1:
            defconfig = defconfig + kasan_config
        content.append("DEFCONFIG={}".format(ctx.attr.defconfig))

        content.append("PRE_DEFCONFIG_CMDS=\"mkdir -p \\${OUT_DIR}/arch/arm64/configs/ && KCONFIG_CONFIG=\\${OUT_DIR}/arch/arm64/configs/${DEFCONFIG} ${ROOT_DIR}/${KERNEL_DIR}/scripts/kconfig/merge_config.sh -m -r " + " ".join(defconfig) + "\"")
        content.append("POST_DEFCONFIG_CMDS=\"\"")
    content.append("")

    if ctx.attr.gki_mixed_build:
        content.append("FILES=\"\"")
    else:
        content.append("FILES=\"${FILES} arch/arm64/boot/Image.lz4 arch/arm64/boot/Image.gz\"")

    content.append("")
    if ctx.attr.mgk_internal[BuildSettingInfo].value:
        content.append("MGK_INTERNAL=true")

    build_config_file = ctx.actions.declare_file("{}/build.config".format(ctx.attr.name))
    ctx.actions.write(
        output = build_config_file,
        content = "\n".join(content) + "\n",
    )
    return DefaultInfo(files = depset([build_config_file]))


mgk_build_config = rule(
    implementation = _mgk_build_config_impl,
    doc = "Defines a kernel build.config target.",
    attrs = {
        "kernel_dir": attr.string(mandatory = True),
        "device_modules_dir": attr.string(mandatory = True),
        "defconfig": attr.string(mandatory = True),
        "defconfig_overlays": attr.string_list(),
        "build_config_overlays": attr.string_list(),
        "kleaf_modules": attr.string_list(),
        "build_variant": attr.string(mandatory = True),
        "gki_mixed_build": attr.bool(),
        "config_is_local": attr.label(
            default = "//build/kernel/kleaf:config_local",
        ),
        "mgk_internal": attr.label(
            default = "@mgk_internal//:mgk_internal",
        ),
    },
)
