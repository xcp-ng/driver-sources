#include <linux/build-salt.h>
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x886158c1, "module_layout" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xe379dad5, "kmalloc_caches" },
	{ 0xd2b09ce5, "__kmalloc" },
	{ 0xf9a482f9, "msleep" },
	{ 0x9e621c1e, "scsi_change_queue_depth" },
	{ 0x9307df03, "pci_free_irq_vectors" },
	{ 0xdbeff42a, "debugfs_create_dir" },
	{ 0x1a0d1ed6, "pci_write_config_word" },
	{ 0xdaf485b9, "pv_lock_ops" },
	{ 0x968ed6aa, "scsi_host_alloc" },
	{ 0x754d539c, "strlen" },
	{ 0x1313b399, "scsi_add_host_with_dma" },
	{ 0x6feb8a2, "scsi_block_requests" },
	{ 0x643b51ab, "_dev_crit" },
	{ 0xc4577c3c, "scsi_unblock_requests" },
	{ 0xa7562771, "pci_disable_device" },
	{ 0xd2c0d036, "scsi_is_fc_rport" },
	{ 0x88bfa7e, "cancel_work_sync" },
	{ 0x9300507b, "mempool_destroy" },
	{ 0x87b8798d, "sg_next" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x2fa0e343, "pcie_capability_clear_and_set_word" },
	{ 0xb348a850, "ex_handler_refcount" },
	{ 0xb5aa7165, "dma_pool_destroy" },
	{ 0x7a2af7b4, "cpu_number" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0x91715312, "sprintf" },
	{ 0x6dbf60, "debugfs_remove_recursive" },
	{ 0x15ba50a6, "jiffies" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xb44ad4b3, "_copy_to_user" },
	{ 0x9624e363, "pci_set_master" },
	{ 0x97934ecf, "del_timer_sync" },
	{ 0x49bf2729, "pci_alloc_irq_vectors_affinity" },
	{ 0xe52c5a04, "_dev_warn" },
	{ 0xfb578fc5, "memset" },
	{ 0x61c88781, "default_llseek" },
	{ 0x59e290af, "pci_restore_state" },
	{ 0x3812050a, "_raw_spin_unlock_irqrestore" },
	{ 0x6b24dc4a, "current_task" },
	{ 0x37befc70, "jiffies_to_msecs" },
	{ 0x7c32d0f0, "printk" },
	{ 0x20c55ae0, "sscanf" },
	{ 0x556c35af, "debugfs_create_file_size" },
	{ 0x449ad0a7, "memcmp" },
	{ 0x7bcf5f59, "__cpu_online_mask" },
	{ 0x90de8db5, "fc_vport_terminate" },
	{ 0x87d61872, "pci_read_config_word" },
	{ 0x7d238261, "debugfs_remove" },
	{ 0x49fcac1c, "scsi_scan_host" },
	{ 0xa48eb0cf, "fc_remote_port_rolechg" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0xe16590e2, "init_uts_ns" },
	{ 0x2f7754a8, "dma_pool_free" },
	{ 0x2072ee9b, "request_threaded_irq" },
	{ 0xfa7fd711, "simple_open" },
	{ 0xd5fc73c4, "scsi_host_put" },
	{ 0x7e1f6608, "_dev_err" },
	{ 0x6e05cb21, "pci_enable_msi" },
	{ 0x9f46ced8, "__sw_hweight64" },
	{ 0x859a9401, "pci_find_capability" },
	{ 0x388703a3, "arch_dma_alloc_attrs" },
	{ 0xe4fae05a, "fc_release_transport" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xdf45a442, "pci_select_bars" },
	{ 0x2c691f7a, "_dev_info" },
	{ 0x8ff4079b, "pv_irq_ops" },
	{ 0x86c45796, "mempool_alloc" },
	{ 0x93a219c, "ioremap_nocache" },
	{ 0xa916b694, "strnlen" },
	{ 0x279f79d4, "pcie_relaxed_ordering_enabled" },
	{ 0x7a6a201f, "pci_cleanup_aer_uncorrect_error_status" },
	{ 0xdb7305a1, "__stack_chk_fail" },
	{ 0xbd7a9c42, "fc_remote_port_delete" },
	{ 0xa202a8e5, "kmalloc_order_trace" },
	{ 0x6a244503, "mempool_create" },
	{ 0x47941711, "_raw_spin_lock_irq" },
	{ 0x70109b5e, "fc_block_scsi_eh" },
	{ 0xaf90ddd4, "pci_read_config_dword" },
	{ 0x6a037cf1, "mempool_kfree" },
	{ 0xcd8dd495, "dma_pool_alloc" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xdee76af2, "pci_unregister_driver" },
	{ 0xd644502b, "kmem_cache_alloc_trace" },
	{ 0x74b55fe, "mempool_free" },
	{ 0x51760917, "_raw_spin_lock_irqsave" },
	{ 0x57842c6f, "pci_try_set_mwi" },
	{ 0x6909ae03, "pci_irq_vector" },
	{ 0x911a3e53, "fc_eh_timed_out" },
	{ 0xa05c03df, "mempool_kmalloc" },
	{ 0x5ed90adc, "int_to_scsilun" },
	{ 0x37a0cba, "kfree" },
	{ 0x131e7ce0, "scsi_dma_unmap" },
	{ 0x69acdf38, "memcpy" },
	{ 0xedc03953, "iounmap" },
	{ 0x918d3197, "__pci_register_driver" },
	{ 0x30a16490, "fc_remove_host" },
	{ 0x7f654dae, "request_firmware" },
	{ 0x74c134b9, "__sw_hweight32" },
	{ 0xc8ba04fe, "scsi_remove_host" },
	{ 0x2e0d2f7f, "queue_work_on" },
	{ 0x29361773, "complete" },
	{ 0x28318305, "snprintf" },
	{ 0x448d6024, "fc_remote_port_add" },
	{ 0x7e75a839, "pci_enable_device_mem" },
	{ 0x77bc13a0, "strim" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0x1e4b79b4, "pci_enable_device" },
	{ 0x4d1ff60a, "wait_for_completion_timeout" },
	{ 0x51d3eaee, "pci_release_selected_regions" },
	{ 0xc9f58a1b, "pci_request_selected_regions" },
	{ 0x16f64495, "dma_pool_create" },
	{ 0xbc1674b9, "fc_attach_transport" },
	{ 0xe492dbce, "release_firmware" },
	{ 0x9e7d6bd0, "__udelay" },
	{ 0x951d56b, "dma_ops" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x507d47aa, "pci_save_state" },
	{ 0xbc098090, "scsi_dma_map" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=scsi_mod,scsi_transport_fc";

MODULE_ALIAS("pci:v00001425d00004600sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004601sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004602sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004603sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004604sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004605sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004606sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004607sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004608sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004609sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000460Asv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000460Bsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000460Csv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000460Dsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000460Esv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004680sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004681sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004682sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004683sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004684sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004685sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004686sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004687sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00004688sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005600sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005601sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005602sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005603sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005604sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005605sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005606sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005607sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005608sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005609sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000560Asv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000560Bsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000560Csv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000560Dsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000560Esv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005610sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005611sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005612sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005613sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005614sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005615sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005616sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005617sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005618sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005619sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000561Asv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000561Bsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005680sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005681sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005682sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005683sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005684sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005685sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005686sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005687sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005688sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005689sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005690sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005691sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005692sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005693sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005694sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005695sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005696sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005697sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005698sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00005699sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Asv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Bsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Csv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Dsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Esv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000569Fsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A0sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A1sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A2sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A3sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A4sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A5sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A6sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A7sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A8sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056A9sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056AAsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056ABsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056ACsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056ADsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056AEsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056AFsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d000056B0sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006601sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006602sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006603sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006604sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006605sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006606sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006607sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006608sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006609sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d0000660Dsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006611sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006614sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006615sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006680sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006681sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006682sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006683sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006684sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006685sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006686sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006687sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006688sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001425d00006689sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "B06C9D4D1C8BCDF3552607A");
