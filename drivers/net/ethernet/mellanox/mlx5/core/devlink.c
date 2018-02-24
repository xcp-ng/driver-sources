// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies */

#include <devlink.h>

#include "mlx5_core.h"
#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
#include "fw_reset.h"
#endif
#include "fs_core.h"
#include "eswitch.h"
#include "mlx5_devm.h"
#include "esw/qos.h"
#include "sf/dev/dev.h"
#include "sf/sf.h"
#include "en/tc_ct.h"

#ifdef HAVE_DEVLINK_DRIVERINIT_VAL
static unsigned int esw_offloads_num_big_groups = ESW_OFFLOADS_DEFAULT_NUM_GROUPS;
#else
unsigned int esw_offloads_num_big_groups = ESW_OFFLOADS_DEFAULT_NUM_GROUPS;
#endif
module_param_named(num_of_groups, esw_offloads_num_big_groups,
		   uint, 0644);
MODULE_PARM_DESC(num_of_groups,
		 "Eswitch offloads number of big groups in FDB table. Valid range 1 - 1024. Default 15");

#ifdef HAVE_DEVLINK_HAS_FLASH_UPDATE
static int mlx5_devlink_flash_update(struct devlink *devlink,
#ifdef HAVE_FLASH_UPDATE_GET_3_PARAMS
				     struct devlink_flash_update_params *params,
#else
				     const char *file_name,
				     const char *component,
#endif
				     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

#ifdef HAVE_DEVLINK_FLASH_UPDATE_PARAMS_HAS_STRUCT_FW
	return mlx5_firmware_flash(dev, params->fw, extack);
#else
	const struct firmware *fw;
	int err;
#ifdef HAVE_FLASH_UPDATE_GET_3_PARAMS
	if (params->component)
#else
	if (component)
#endif
		return -EOPNOTSUPP;

	err = request_firmware_direct(&fw,
#ifdef HAVE_FLASH_UPDATE_GET_3_PARAMS
			params->file_name,
#else
			file_name,
#endif
			&dev->pdev->dev);
	if (err)
		return err;

	err = mlx5_firmware_flash(dev, fw, extack);
	release_firmware(fw);

	return err;
#endif /* HAVE_DEVLINK_FLASH_UPDATE_PARAMS_HAS_STRUCT_FW */
}
#endif /* HAVE_DEVLINK_HAS_FLASH_UPDATE */

#if defined(HAVE_DEVLINK_HAS_INFO_GET) && defined(HAVE_DEVLINK_INFO_VERSION_FIXED_PUT)
static u8 mlx5_fw_ver_major(u32 version)
{
	return (version >> 24) & 0xff;
}

static u8 mlx5_fw_ver_minor(u32 version)
{
	return (version >> 16) & 0xff;
}

static u16 mlx5_fw_ver_subminor(u32 version)
{
	return version & 0xffff;
}

#define DEVLINK_FW_STRING_LEN 32
static int
mlx5_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
		      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	char version_str[DEVLINK_FW_STRING_LEN];
	u32 running_fw, stored_fw;
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err)
		return err;

	err = devlink_info_version_fixed_put(req, "fw.psid", dev->board_id);
	if (err)
		return err;

	err = mlx5_fw_version_query(dev, &running_fw, &stored_fw);
	if (err)
		return err;

	snprintf(version_str, sizeof(version_str), "%d.%d.%04d",
		 mlx5_fw_ver_major(running_fw), mlx5_fw_ver_minor(running_fw),
		 mlx5_fw_ver_subminor(running_fw));
	err = devlink_info_version_running_put(req, "fw.version", version_str);
	if (err)
		return err;
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       version_str);
	if (err)
		return err;

	/* no pending version, return running (stored) version */
	if (stored_fw == 0)
		stored_fw = running_fw;

	snprintf(version_str, sizeof(version_str), "%d.%d.%04d",
		 mlx5_fw_ver_major(stored_fw), mlx5_fw_ver_minor(stored_fw),
		 mlx5_fw_ver_subminor(stored_fw));
	err = devlink_info_version_stored_put(req, "fw.version", version_str);
	if (err)
		return err;
	return devlink_info_version_stored_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW,
					       version_str);
}
#endif

#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
static int mlx5_devlink_reload_fw_activate(struct devlink *devlink, struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u8 reset_level, reset_type, net_port_alive;
	int err;

	err = mlx5_fw_reset_query(dev, &reset_level, &reset_type);
	if (err)
		return err;
	if (!(reset_level & MLX5_MFRL_REG_RESET_LEVEL3)) {
		NL_SET_ERR_MSG_MOD(extack, "FW activate requires reboot");
		return -EINVAL;
	}

	net_port_alive = !!(reset_type & MLX5_MFRL_REG_RESET_TYPE_NET_PORT_ALIVE);
	err = mlx5_fw_reset_set_reset_sync(dev, net_port_alive, extack);
	if (err)
		return err;

	err = mlx5_fw_reset_wait_reset_done(dev);
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER 
	if (err)
		return err;

	mlx5_unload_one_devl_locked(dev);
	err = mlx5_health_wait_pci_up(dev);
	if (err)
		NL_SET_ERR_MSG_MOD(extack, "FW activate aborted, PCI reads fail after reset");
#endif
	return err;
}

static int mlx5_devlink_trigger_fw_live_patch(struct devlink *devlink,
					      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u8 reset_level;
	int err;

	err = mlx5_fw_reset_query(dev, &reset_level, NULL);
	if (err)
		return err;
	if (!(reset_level & MLX5_MFRL_REG_RESET_LEVEL0)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "FW upgrade to the stored FW can't be done by FW live patching");
		return -EINVAL;
	}

	return mlx5_fw_reset_set_live_patch(dev);
}
#endif

#ifdef HAVE_DEVLINK_HAS_RELOAD_UP_DOWN
static int mlx5_devlink_reload_down(struct devlink *devlink,
#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
				    bool netns_change,
				    enum devlink_reload_action action,
				    enum devlink_reload_limit limit,
#elif defined(HAVE_DEVLINK_RELOAD_DOWN_HAS_3_PARAMS)
			     	    bool netns_change,
#endif
 				    struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct pci_dev *pdev = dev->pdev;
	bool sf_dev_allocated;
	int ret = 0;

	sf_dev_allocated = mlx5_sf_dev_allocated(dev);
	if (sf_dev_allocated) {
		/* Reload results in deleting SF device which further results in
		 * unregistering devlink instance while holding devlink_mutext.
		 * Hence, do not support reload.
		 */
		NL_SET_ERR_MSG_MOD(extack, "reload is unsupported when SFs are allocated");
		return -EOPNOTSUPP;
	}

	if (mlx5_lag_is_active(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "reload is unsupported in Lag mode");
		return -EOPNOTSUPP;
	}

	if (pci_num_vf(pdev)) {
		NL_SET_ERR_MSG_MOD(extack, "reload while VFs are present is unfavorable");
	}

#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
		mlx5_unload_one_devl_locked(dev);
#else
		mlx5_unload_one(dev);
#endif
		break;
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		if (limit == DEVLINK_RELOAD_LIMIT_NO_RESET)
			ret = mlx5_devlink_trigger_fw_live_patch(devlink, extack);
		else
			ret = mlx5_devlink_reload_fw_activate(devlink, extack);
		break;
	default:
		/* Unsupported action should not get to this function */
		WARN_ON(1);
		ret = -EOPNOTSUPP;
	}
#else /* HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION */
	mlx5_unload_one(dev);
#endif /* HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION */
	return ret;
}

static int mlx5_devlink_reload_up(struct devlink *devlink,
#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
				  enum devlink_reload_action action,
				  enum devlink_reload_limit limit, u32 *actions_performed,
#endif
 				  struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	int ret = 0;

#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
	*actions_performed = BIT(action);
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
		ret = mlx5_load_one_devl_locked(dev, false);
#else
		ret = mlx5_load_one(dev, false);
#endif
		break;
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		if (limit == DEVLINK_RELOAD_LIMIT_NO_RESET)
			break;
		/* On fw_activate action, also driver is reloaded and reinit performed */
		*actions_performed |= BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
		ret = mlx5_load_one_devl_locked(dev, false);
#else
		ret = mlx5_load_one(dev, false);
#endif
		break;
	default:
		/* Unsupported action should not get to this function */
		WARN_ON(1);
		ret = -EOPNOTSUPP;
	}

#else /* HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION*/
	ret = mlx5_load_one(dev, false);
#endif /* HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION */
	return ret;
}
#endif /* HAVE_DEVLINK_HAS_RELOAD_UP_DOWN */

#if defined(HAVE_DEVLINK_HAS_RELOAD)
static int load_one_and_check(struct mlx5_core_dev *dev,
                             struct netlink_ext_ack *extack)
{
       int err;

       err = mlx5_load_one(dev, false);
       if (err == -EUSERS)
               NL_SET_ERR_MSG_MOD(extack, "IRQs for requested CPU affinity are not available");
       return err;
}

static int mlx5_devlink_reload(struct devlink *devlink,
                              struct netlink_ext_ack *extack)
{
       struct mlx5_core_dev *dev = devlink_priv(devlink);

       mlx5_unload_one(dev);
       return load_one_and_check(dev, extack);
}
#endif

#ifdef HAVE_DEVLINK_TRAP_SUPPORT
static struct mlx5_devlink_trap *mlx5_find_trap_by_id(struct mlx5_core_dev *dev, int trap_id)
{
	struct mlx5_devlink_trap *dl_trap;

	list_for_each_entry(dl_trap, &dev->priv.traps, list)
		if (dl_trap->trap.id == trap_id)
			return dl_trap;

	return NULL;
}

static int mlx5_devlink_trap_init(struct devlink *devlink, const struct devlink_trap *trap,
				  void *trap_ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_devlink_trap *dl_trap;

	dl_trap = kzalloc(sizeof(*dl_trap), GFP_KERNEL);
	if (!dl_trap)
		return -ENOMEM;

	dl_trap->trap.id = trap->id;
	dl_trap->trap.action = DEVLINK_TRAP_ACTION_DROP;
	dl_trap->item = trap_ctx;

	if (mlx5_find_trap_by_id(dev, trap->id)) {
		kfree(dl_trap);
		mlx5_core_err(dev, "Devlink trap: Trap 0x%x already found", trap->id);
		return -EEXIST;
	}

	list_add_tail(&dl_trap->list, &dev->priv.traps);
	return 0;
}

static void mlx5_devlink_trap_fini(struct devlink *devlink, const struct devlink_trap *trap,
				   void *trap_ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_devlink_trap *dl_trap;

	dl_trap = mlx5_find_trap_by_id(dev, trap->id);
	if (!dl_trap) {
		mlx5_core_err(dev, "Devlink trap: Missing trap id 0x%x", trap->id);
		return;
	}
	list_del(&dl_trap->list);
	kfree(dl_trap);
}

static int mlx5_devlink_trap_action_set(struct devlink *devlink,
					const struct devlink_trap *trap,
#ifdef HAVE_DEVLINK_TRAP_ACTION_SET_4_ARGS
					enum devlink_trap_action action,
					struct netlink_ext_ack *extack)
#else
					enum devlink_trap_action action)
#endif
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	struct mlx5_devlink_trap_event_ctx trap_event_ctx;
	enum devlink_trap_action action_orig;
	struct mlx5_devlink_trap *dl_trap;
	int err = 0;

	if (is_mdev_switchdev_mode(dev)) {
#ifdef HAVE_DEVLINK_TRAP_ACTION_SET_4_ARGS
		NL_SET_ERR_MSG_MOD(extack, "Devlink traps can't be set in switchdev mode");
#endif
		return -EOPNOTSUPP;
	}

	dl_trap = mlx5_find_trap_by_id(dev, trap->id);
	if (!dl_trap) {
		mlx5_core_err(dev, "Devlink trap: Set action on invalid trap id 0x%x", trap->id);
		err = -EINVAL;
		goto out;
	}

	if (action != DEVLINK_TRAP_ACTION_DROP && action != DEVLINK_TRAP_ACTION_TRAP) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (action == dl_trap->trap.action)
		goto out;

	action_orig = dl_trap->trap.action;
	dl_trap->trap.action = action;
	trap_event_ctx.trap = &dl_trap->trap;
	trap_event_ctx.err = 0;
	err = mlx5_blocking_notifier_call_chain(dev, MLX5_DRIVER_EVENT_TYPE_TRAP,
						&trap_event_ctx);
	if (err == NOTIFY_BAD)
		dl_trap->trap.action = action_orig;
	err = trap_event_ctx.err;
out:
	return err;
}
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */

static const struct devlink_ops mlx5_devlink_ops = {
#ifdef CONFIG_MLX5_ESWITCH
#ifdef HAVE_DEVLINK_HAS_ESWITCH_MODE_GET_SET
	.eswitch_mode_set = mlx5_devlink_eswitch_mode_set,
	.eswitch_mode_get = mlx5_devlink_eswitch_mode_get,
#endif /* HAVE_DEVLINK_HAS_ESWITCH_MODE_GET_SET */
#ifdef HAVE_DEVLINK_HAS_ESWITCH_INLINE_MODE_GET_SET
	.eswitch_inline_mode_set = mlx5_devlink_eswitch_inline_mode_set,
	.eswitch_inline_mode_get = mlx5_devlink_eswitch_inline_mode_get,
#endif /* HAVE_DEVLINK_HAS_ESWITCH_INLINE_MODE_GET_SET */
#ifdef HAVE_DEVLINK_HAS_ESWITCH_ENCAP_MODE_SET
	.eswitch_encap_mode_set = mlx5_devlink_eswitch_encap_mode_set,
	.eswitch_encap_mode_get = mlx5_devlink_eswitch_encap_mode_get,
#endif /* HAVE_DEVLINK_HAS_ESWITCH_ENCAP_MODE_SET */
#ifdef HAVE_DEVLINK_HAS_PORT_FUNCTION_HW_ADDR_GET
	.port_function_hw_addr_get = mlx5_devlink_port_function_hw_addr_get,
	.port_function_hw_addr_set = mlx5_devlink_port_function_hw_addr_set,
#ifdef HAVE_DEVLINK_HAS_RATE_FUNCTIONS
	.rate_leaf_tx_share_set = mlx5_esw_devlink_rate_leaf_tx_share_set,
	.rate_leaf_tx_max_set = mlx5_esw_devlink_rate_leaf_tx_max_set,
	.rate_node_tx_share_set = mlx5_esw_devlink_rate_node_tx_share_set,
	.rate_node_tx_max_set = mlx5_esw_devlink_rate_node_tx_max_set,
	.rate_node_new = mlx5_esw_devlink_rate_node_new,
	.rate_node_del = mlx5_esw_devlink_rate_node_del,
	.rate_leaf_parent_set = mlx5_esw_devlink_rate_parent_set,
#endif
#endif
#endif /* CONFIG_MLX5_ESWITCH */

#if defined(CONFIG_MLX5_SF_MANAGER) && \
   (defined(HAVE_DEVLINK_PORT_ATTRS_PCI_SF_SET_GET_4_PARAMS) || defined(HAVE_DEVLINK_PORT_ATTRS_PCI_SF_SET_GET_5_PARAMS))
	.port_new = mlx5_devlink_sf_port_new,
	.port_del = mlx5_devlink_sf_port_del,
#endif
#if defined(CONFIG_MLX5_SF_MANAGER) && \
    defined(HAVE_DEVLINK_HAS_PORT_FUNCTION_STATE_GET)
	.port_fn_state_get = mlx5_devlink_sf_port_fn_state_get,
	.port_fn_state_set = mlx5_devlink_sf_port_fn_state_set,
#endif
#ifdef HAVE_DEVLINK_HAS_FLASH_UPDATE
	.flash_update = mlx5_devlink_flash_update,
#endif /* HAVE_DEVLINK_HAS_FLASH_UPDATE */
#if defined(HAVE_DEVLINK_HAS_INFO_GET) && defined(HAVE_DEVLINK_INFO_VERSION_FIXED_PUT)
	.info_get = mlx5_devlink_info_get,
#endif /* HAVE_DEVLINK_HAS_INFO_GET && HAVE_DEVLINK_INFO_VERSION_FIXED_PUT */
#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
			  BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_limits = BIT(DEVLINK_RELOAD_LIMIT_NO_RESET),
#endif
#ifdef HAVE_DEVLINK_HAS_RELOAD_UP_DOWN
	.reload_down = mlx5_devlink_reload_down,
	.reload_up = mlx5_devlink_reload_up,
#endif /* HAVE_DEVLINK_HAS_RELOAD_UP_DOWN */
#ifdef HAVE_DEVLINK_HAS_RELOAD
       .reload = mlx5_devlink_reload,
#endif
#ifdef HAVE_DEVLINK_TRAP_SUPPORT
	.trap_init = mlx5_devlink_trap_init,
	.trap_fini = mlx5_devlink_trap_fini,
	.trap_action_set = mlx5_devlink_trap_action_set,
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */
};

#ifdef HAVE_DEVLINK_TRAP_SUPPORT
void mlx5_devlink_trap_report(struct mlx5_core_dev *dev, int trap_id, struct sk_buff *skb,
			      struct devlink_port *dl_port)
{
	struct devlink *devlink = priv_to_devlink(dev);
	struct mlx5_devlink_trap *dl_trap;

	dl_trap = mlx5_find_trap_by_id(dev, trap_id);
	if (!dl_trap) {
		mlx5_core_err(dev, "Devlink trap: Report on invalid trap id 0x%x", trap_id);
		return;
	}

	if (dl_trap->trap.action != DEVLINK_TRAP_ACTION_TRAP) {
		mlx5_core_dbg(dev, "Devlink trap: Trap id %d has action %d", trap_id,
			      dl_trap->trap.action);
		return;
	}
#ifdef HAVE_DEVLINK_TRAP_REPORT_5_ARGS
	devlink_trap_report(devlink, skb, dl_trap->item, dl_port, NULL);
#else
	devlink_trap_report(devlink, skb, dl_trap->item, dl_port);
#endif
}

int mlx5_devlink_trap_get_num_active(struct mlx5_core_dev *dev)
{
	struct mlx5_devlink_trap *dl_trap;
	int count = 0;

	list_for_each_entry(dl_trap, &dev->priv.traps, list)
		if (dl_trap->trap.action == DEVLINK_TRAP_ACTION_TRAP)
			count++;

	return count;
}

int mlx5_devlink_traps_get_action(struct mlx5_core_dev *dev, int trap_id,
				  enum devlink_trap_action *action)
{
	struct mlx5_devlink_trap *dl_trap;

	dl_trap = mlx5_find_trap_by_id(dev, trap_id);
	if (!dl_trap) {
		mlx5_core_err(dev, "Devlink trap: Get action on invalid trap id 0x%x",
			      trap_id);
		return -EINVAL;
	}

	*action = dl_trap->trap.action;
	return 0;
}
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */

struct devlink *mlx5_devlink_alloc(struct device *dev)
{
#ifdef HAVE_DEVLINK_ALLOC_GET_3_PARAMS
	return devlink_alloc(&mlx5_devlink_ops, sizeof(struct mlx5_core_dev),
			     dev);
#else
	return devlink_alloc(&mlx5_devlink_ops, sizeof(struct mlx5_core_dev));
#endif
}

void mlx5_devlink_free(struct devlink *devlink)
{
	devlink_free(devlink);
}

#if defined(HAVE_DEVLINK_PARAM) && (defined(HAVE_DEVLINK_PARAMS_PUBLISHED) || defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS))
static int mlx5_devlink_fs_mode_validate(struct devlink *devlink, u32 id,
					 union devlink_param_value val,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	char *value = val.vstr;
	int err = 0;

	if (!strcmp(value, "dmfs")) {
		return 0;
	} else if (!strcmp(value, "smfs")) {
		u8 eswitch_mode;
		bool smfs_cap;

		eswitch_mode = mlx5_eswitch_mode(dev);
		smfs_cap = mlx5_fs_dr_is_supported(dev);

		if (!smfs_cap) {
			err = -EOPNOTSUPP;
			NL_SET_ERR_MSG_MOD(extack,
					   "Software managed steering is not supported by current device");
		}

		else if (eswitch_mode == MLX5_ESWITCH_OFFLOADS) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Software managed steering is not supported when eswitch offloads enabled.");
			err = -EOPNOTSUPP;
		}
	} else {
		NL_SET_ERR_MSG_MOD(extack,
				   "Bad parameter: supported values are [\"dmfs\", \"smfs\"]");
		err = -EINVAL;
	}

	return err;
}

static int mlx5_devlink_fs_mode_set(struct devlink *devlink, u32 id,
				    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	enum mlx5_flow_steering_mode mode;

	if (!strcmp(ctx->val.vstr, "smfs"))
		mode = MLX5_FLOW_STEERING_MODE_SMFS;
	else
		mode = MLX5_FLOW_STEERING_MODE_DMFS;
	dev->priv.steering->mode = mode;

	return 0;
}

static int mlx5_devlink_fs_mode_get(struct devlink *devlink, u32 id,
				    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (dev->priv.steering->mode == MLX5_FLOW_STEERING_MODE_SMFS)
		strcpy(ctx->val.vstr, "smfs");
	else
		strcpy(ctx->val.vstr, "dmfs");
	return 0;
}

#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE
static int mlx5_devlink_enable_roce_validate(struct devlink *devlink, u32 id,
					     union devlink_param_value val,
					     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	bool new_state = val.vbool;

	if (new_state && !MLX5_CAP_GEN(dev, roce) &&
	    !(MLX5_CAP_GEN(dev, roce_rw_supported) && MLX5_CAP_GEN_MAX(dev, roce))) {
		NL_SET_ERR_MSG_MOD(extack, "Device doesn't support RoCE");
		return -EOPNOTSUPP;
	}
	if (mlx5_core_is_mp_slave(dev) || mlx5_lag_is_active(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Multi port slave/Lag device can't configure RoCE");
		return -EOPNOTSUPP;
	}

	return 0;
}
#endif

#ifdef CONFIG_MLX5_ESWITCH
static int mlx5_devlink_large_group_num_validate(struct devlink *devlink, u32 id,
						 union devlink_param_value val,
						 struct netlink_ext_ack *extack)
{
	int group_num = val.vu32;

	if (group_num < 1 || group_num > 1024) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unsupported group number, supported range is 1-1024");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mlx5_devlink_esw_port_metadata_set(struct devlink *devlink, u32 id,
					      struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!MLX5_ESWITCH_MANAGER(dev))
		return -EOPNOTSUPP;

	return mlx5_esw_offloads_vport_metadata_set(dev->priv.eswitch, ctx->val.vbool);
}

static int mlx5_devlink_esw_port_metadata_get(struct devlink *devlink, u32 id,
					      struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!MLX5_ESWITCH_MANAGER(dev))
		return -EOPNOTSUPP;

	ctx->val.vbool = mlx5_eswitch_vport_match_metadata_enabled(dev->priv.eswitch);
	return 0;
}

static int mlx5_devlink_esw_port_metadata_validate(struct devlink *devlink, u32 id,
						   union devlink_param_value val,
						   struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u8 esw_mode;

	if (!MLX5_ESWITCH_MANAGER(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch is unsupported");
		return -EOPNOTSUPP;
	}
	esw_mode = mlx5_eswitch_mode(dev);
	if (esw_mode == MLX5_ESWITCH_OFFLOADS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-Switch must either disabled or non switchdev mode");
		return -EBUSY;
	}
	return 0;
}

static int mlx5_devlink_esw_pet_insert_set(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!MLX5_ESWITCH_MANAGER(dev))
		return -EOPNOTSUPP;

	return mlx5_esw_offloads_pet_insert_set(dev->priv.eswitch, ctx->val.vbool);
}

static int mlx5_devlink_esw_pet_insert_get(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!MLX5_ESWITCH_MANAGER(dev))
		return -EOPNOTSUPP;

	ctx->val.vbool = mlx5_eswitch_pet_insert_allowed(dev->priv.eswitch);
	return 0;
}

static int mlx5_devlink_esw_pet_insert_validate(struct devlink *devlink, u32 id,
						union devlink_param_value val,
						struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u8 esw_mode;

	if (!MLX5_ESWITCH_MANAGER(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "E-Switch is unsupported");
		return -EOPNOTSUPP;
	}

	esw_mode = mlx5_eswitch_mode(dev);
	if (esw_mode == MLX5_ESWITCH_OFFLOADS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "E-Switch must either disabled or non switchdev mode");
		return -EBUSY;
	}

	if (!mlx5e_esw_offloads_pet_supported(dev->priv.eswitch))
		return -EOPNOTSUPP;

	if (!mlx5_core_is_ecpf(dev))
		return -EOPNOTSUPP;

	return 0;
}
#endif

#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
static int mlx5_devlink_enable_remote_dev_reset_set(struct devlink *devlink, u32 id,
						    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	mlx5_fw_reset_enable_remote_dev_reset_set(dev, ctx->val.vbool);
	return 0;
}

static int mlx5_devlink_enable_remote_dev_reset_get(struct devlink *devlink, u32 id,
						    struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	ctx->val.vbool = mlx5_fw_reset_enable_remote_dev_reset_get(dev);
	return 0;
}
#endif

#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE
static int mlx5_devlink_eq_depth_validate(struct devlink *devlink, u32 id,
					  union devlink_param_value val,
					  struct netlink_ext_ack *extack)
{
	return (val.vu32 >= 64 && val.vu32 <= 4096) ? 0 : -EINVAL;
}
#endif

static int mlx5_devlink_ct_max_offloaded_conns_set(struct devlink *devlink, u32 id,
						   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	mlx5_tc_ct_max_offloaded_conns_set(dev, ctx->val.vu32);
	return 0;
}

static int mlx5_devlink_ct_max_offloaded_conns_get(struct devlink *devlink, u32 id,
						   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	ctx->val.vu32 = mlx5_tc_ct_max_offloaded_conns_get(dev);
	return 0;
}

static const struct devlink_param mlx5_devlink_params[] = {
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_CT_ACTION_ON_NAT_CONNS,
			     "ct_action_on_nat_conns", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_devlink_ct_action_on_nat_conns_get,
			     mlx5_devlink_ct_action_on_nat_conns_set,
			     NULL),
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_FLOW_STEERING_MODE,
			     "flow_steering_mode", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_devlink_fs_mode_get, mlx5_devlink_fs_mode_set,
			     mlx5_devlink_fs_mode_validate),
#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE
	DEVLINK_PARAM_GENERIC(ENABLE_ROCE, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx5_devlink_enable_roce_validate),
#endif
#ifdef HAVE_DEVLINK_RELOAD_DOWN_SUPPORT_RELOAD_ACTION
	DEVLINK_PARAM_GENERIC(ENABLE_REMOTE_DEV_RESET, BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			mlx5_devlink_enable_remote_dev_reset_get,
			mlx5_devlink_enable_remote_dev_reset_set, NULL),
#endif
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_CT_MAX_OFFLOADED_CONNS,
			     "ct_max_offloaded_conns", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_devlink_ct_max_offloaded_conns_get,
			     mlx5_devlink_ct_max_offloaded_conns_set,
			     NULL),
#ifdef CONFIG_MLX5_ESWITCH
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_ESW_LARGE_GROUP_NUM,
			     "fdb_large_groups", DEVLINK_PARAM_TYPE_U32,
			     BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			     NULL, NULL,
			     mlx5_devlink_large_group_num_validate),
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_ESW_PORT_METADATA,
			     "esw_port_metadata", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_devlink_esw_port_metadata_get,
			     mlx5_devlink_esw_port_metadata_set,
			     mlx5_devlink_esw_port_metadata_validate),
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_ESW_PET_INSERT,
			     "esw_pet_insert", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     mlx5_devlink_esw_pet_insert_get,
			     mlx5_devlink_esw_pet_insert_set,
			     mlx5_devlink_esw_pet_insert_validate),
#endif
#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE
	DEVLINK_PARAM_GENERIC(IO_EQ_SIZE, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx5_devlink_eq_depth_validate),
	DEVLINK_PARAM_GENERIC(EVENT_EQ_SIZE, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx5_devlink_eq_depth_validate),
#endif
};

static void mlx5_devlink_set_params_init_values(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	union devlink_param_value value;

#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE
	value.vbool = MLX5_CAP_GEN(dev, roce);
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,
					   value);
#endif

#ifdef CONFIG_MLX5_ESWITCH
	value.vu32 = esw_offloads_num_big_groups;
	devlink_param_driverinit_value_set(devlink,
					   MLX5_DEVLINK_PARAM_ID_ESW_LARGE_GROUP_NUM,
					   value);

	if (MLX5_ESWITCH_MANAGER(dev)) {
		value.vbool = false;
		devlink_param_driverinit_value_set(devlink,
						   MLX5_DEVLINK_PARAM_ID_ESW_PET_INSERT,
						   value);
	}
#endif

#ifdef HAVE_DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE
	value.vu32 = MLX5_COMP_EQ_SIZE;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_IO_EQ_SIZE,
					   value);

	value.vu32 = MLX5_NUM_ASYNC_EQE;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_EVENT_EQ_SIZE,
					   value);
#endif
}
#endif /* defined(HAVE_DEVLINK_PARAM) && (defined(HAVE_DEVLINK_PARAMS_PUBLISHED) || defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS) */

#ifdef HAVE_DEVLINK_PARAM_REGISTER
static const struct devlink_param enable_eth_param =
	DEVLINK_PARAM_GENERIC(ENABLE_ETH, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, NULL);

static int mlx5_devlink_eth_param_register(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	union devlink_param_value value;
	int err;

	if (!mlx5_eth_supported(dev))
		return 0;

	err = devlink_param_register(devlink, &enable_eth_param);
	if (err)
		return err;

	value.vbool = true;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_ENABLE_ETH,
					   value);
#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	devlink_param_publish(devlink, &enable_eth_param);
#endif
	return 0;
}

static void mlx5_devlink_eth_param_unregister(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!mlx5_eth_supported(dev))
		return;

#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	devlink_param_unpublish(devlink, &enable_eth_param);
#endif
	devlink_param_unregister(devlink, &enable_eth_param);
}

static int mlx5_devlink_enable_rdma_validate(struct devlink *devlink, u32 id,
					     union devlink_param_value val,
					     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	bool new_state = val.vbool;

	if (new_state && !mlx5_rdma_supported(dev))
		return -EOPNOTSUPP;
	return 0;
}

static const struct devlink_param enable_rdma_param =
	DEVLINK_PARAM_GENERIC(ENABLE_RDMA, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx5_devlink_enable_rdma_validate);

static int mlx5_devlink_rdma_param_register(struct devlink *devlink)
{
	union devlink_param_value value;
	int err;

	if (!IS_ENABLED(CONFIG_MLX5_INFINIBAND))
		return 0;

	err = devlink_param_register(devlink, &enable_rdma_param);
	if (err)
		return err;

	value.vbool = true;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_ENABLE_RDMA,
					   value);
#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	devlink_param_publish(devlink, &enable_rdma_param);
#endif
	return 0;
}

static void mlx5_devlink_rdma_param_unregister(struct devlink *devlink)
{
	if (!IS_ENABLED(CONFIG_MLX5_INFINIBAND))
		return;

#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	devlink_param_unpublish(devlink, &enable_rdma_param);
#endif
	devlink_param_unregister(devlink, &enable_rdma_param);
}

static const struct devlink_param enable_vnet_param =
	DEVLINK_PARAM_GENERIC(ENABLE_VNET, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, NULL);

static int mlx5_devlink_vnet_param_register(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	union devlink_param_value value;
	int err;

	if (!mlx5_vnet_supported(dev))
		return 0;

	err = devlink_param_register(devlink, &enable_vnet_param);
	if (err)
		return err;

	value.vbool = true;
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_ENABLE_VNET,
					   value);
#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	 devlink_param_publish(devlink, &enable_vnet_param);
#endif
	return 0;
}

static void mlx5_devlink_vnet_param_unregister(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!mlx5_vnet_supported(dev))
		return;

#ifdef HAVE_DEVLINK_PARAM_PUBLISH
	 devlink_param_unpublish(devlink, &enable_vnet_param);
#endif
	devlink_param_unregister(devlink, &enable_vnet_param);
}

static int mlx5_devlink_auxdev_params_register(struct devlink *devlink)
{
	int err;

	err = mlx5_devlink_eth_param_register(devlink);
	if (err)
		return err;

	err = mlx5_devlink_rdma_param_register(devlink);
	if (err)
		goto rdma_err;

	err = mlx5_devlink_vnet_param_register(devlink);
	if (err)
		goto vnet_err;
	return 0;

vnet_err:
	mlx5_devlink_rdma_param_unregister(devlink);
rdma_err:
	mlx5_devlink_eth_param_unregister(devlink);
	return err;
}

static void mlx5_devlink_auxdev_params_unregister(struct devlink *devlink)
{
	mlx5_devlink_vnet_param_unregister(devlink);
	mlx5_devlink_rdma_param_unregister(devlink);
	mlx5_devlink_eth_param_unregister(devlink);
}

static int mlx5_devlink_max_uc_list_validate(struct devlink *devlink, u32 id,
					     union devlink_param_value val,
					     struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (val.vu32 == 0) {
		NL_SET_ERR_MSG_MOD(extack, "max_macs value must be greater than 0");
		return -EINVAL;
	}

	if (!is_power_of_2(val.vu32)) {
		NL_SET_ERR_MSG_MOD(extack, "Only power of 2 values are supported for max_macs");
		return -EINVAL;
	}

	if (ilog2(val.vu32) >
	    MLX5_CAP_GEN_MAX(dev, log_max_current_uc_list)) {
		NL_SET_ERR_MSG_MOD(extack, "max_macs value is out of the supported range");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param max_uc_list_param =
	DEVLINK_PARAM_GENERIC(MAX_MACS, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL, mlx5_devlink_max_uc_list_validate);

static int mlx5_devlink_max_uc_list_param_register(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	union devlink_param_value value;
	int err;

	if (!MLX5_CAP_GEN_MAX(dev, log_max_current_uc_list_wr_supported))
		return 0;

	err = devlink_param_register(devlink, &max_uc_list_param);
	if (err)
		return err;

	value.vu32 = 1 << MLX5_CAP_GEN(dev, log_max_current_uc_list);
	devlink_param_driverinit_value_set(devlink,
					   DEVLINK_PARAM_GENERIC_ID_MAX_MACS,
					   value);
	return 0;
}

static void
mlx5_devlink_max_uc_list_param_unregister(struct devlink *devlink)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	if (!MLX5_CAP_GEN_MAX(dev, log_max_current_uc_list_wr_supported))
		return;

	devlink_param_unregister(devlink, &max_uc_list_param);
}
#endif /* HAVE_DEVLINK_PARAM_PUBLISH */

#ifdef HAVE_DEVLINK_TRAP_SUPPORT
#ifdef HAVE_DEVLINK_TRAP_GROUPS_REGISTER
#define MLX5_TRAP_DROP(_id, _group_id)					\
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				\
			     DEVLINK_TRAP_GROUP_GENERIC_ID_##_group_id, \
			     DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT)
#else
#define MLX5_TRAP_DROP(_id, group)					\
	DEVLINK_TRAP_GENERIC(DROP, DROP, _id,				\
			     DEVLINK_TRAP_GROUP_GENERIC(group),         \
			     DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT)
#endif

static const struct devlink_trap mlx5_traps_arr[] = {
	MLX5_TRAP_DROP(INGRESS_VLAN_FILTER, L2_DROPS),
#ifdef HAVE_DEVLINK_TRAP_DMAC_FILTER
	MLX5_TRAP_DROP(DMAC_FILTER, L2_DROPS),
#endif
};

static const struct devlink_trap_group mlx5_trap_groups_arr[] = {
#ifdef HAVE_DEVLINK_TRAP_GROUP_GENERIC_2_ARGS
	DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS, 0),
#else
	DEVLINK_TRAP_GROUP_GENERIC(L2_DROPS),
#endif
};

static int mlx5_devlink_traps_register(struct devlink *devlink)
{
	struct mlx5_core_dev *core_dev = devlink_priv(devlink);
	int err;

#ifdef HAVE_DEVLINK_TRAP_GROUPS_REGISTER
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
	err = devl_trap_groups_register(devlink, mlx5_trap_groups_arr,
					ARRAY_SIZE(mlx5_trap_groups_arr));
#else
	err = devlink_trap_groups_register(devlink, mlx5_trap_groups_arr,
					   ARRAY_SIZE(mlx5_trap_groups_arr));
#endif /* HAVE_DEVL_TRAP_GROUPS_REGISTER */
	if (err)
		return err;
#endif /* HAVE_DEVLINK_TRAP_GROUPS_REGISTER */

#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
	err = devl_traps_register(devlink, mlx5_traps_arr, ARRAY_SIZE(mlx5_traps_arr),
				  &core_dev->priv);
#else
	err = devlink_traps_register(devlink, mlx5_traps_arr, ARRAY_SIZE(mlx5_traps_arr),
				     &core_dev->priv);
#endif
#ifdef HAVE_DEVLINK_TRAP_GROUPS_REGISTER
	if (err)
		goto err_trap_group;
	return 0;

err_trap_group:
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
	devl_trap_groups_unregister(devlink, mlx5_trap_groups_arr,
				    ARRAY_SIZE(mlx5_trap_groups_arr));
#else
	devlink_trap_groups_unregister(devlink, mlx5_trap_groups_arr,
				       ARRAY_SIZE(mlx5_trap_groups_arr));
#endif /* HAVE_DEVL_TRAP_GROUPS_REGISTER */
#endif /* HAVE_DEVLINK_TRAP_GROUPS_REGISTER */
	return err;
}

static void mlx5_devlink_traps_unregister(struct devlink *devlink)
{
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
	devl_traps_unregister(devlink, mlx5_traps_arr, ARRAY_SIZE(mlx5_traps_arr));
#else
	devlink_traps_unregister(devlink, mlx5_traps_arr, ARRAY_SIZE(mlx5_traps_arr));
#endif
#ifdef HAVE_DEVLINK_TRAP_GROUPS_REGISTER
#ifdef HAVE_DEVL_TRAP_GROUPS_REGISTER
	devl_trap_groups_unregister(devlink, mlx5_trap_groups_arr,
				    ARRAY_SIZE(mlx5_trap_groups_arr));
#else
	devlink_trap_groups_unregister(devlink, mlx5_trap_groups_arr,
				       ARRAY_SIZE(mlx5_trap_groups_arr));
#endif /* HAVE_DEVL_TRAP_GROUPS_REGISTER */
#endif /* HAVE_DEVLINK_TRAP_GROUPS_REGISTER */
}
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */

#ifdef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
int mlx5_devlink_register(struct devlink *devlink)
#else
int mlx5_devlink_register(struct devlink *devlink, struct device *pdev)
#endif
{
#ifdef HAVE_DEVLINK_SET_FEATURES
	struct mlx5_core_dev *dev = devlink_priv(devlink);
#endif
#if (!defined(HAVE_DEVLINK_PARAM) || (!defined(HAVE_DEVLINK_PARAMS_PUBLISHED) && !defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS))) && defined(CONFIG_MLX5_ESWITCH)
	struct mlx5_core_dev *priv_dev;
	struct mlx5_eswitch *eswitch;
#endif
	int err;

#ifndef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
	err = devlink_register(devlink, pdev);
	if (err)
		return err;
#endif

#if defined(HAVE_DEVLINK_PARAM) && (defined(HAVE_DEVLINK_PARAMS_PUBLISHED) || defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS))
	err = devlink_params_register(devlink, mlx5_devlink_params,
				      ARRAY_SIZE(mlx5_devlink_params));
	if (err)
#ifdef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
		return err;
#else
		goto params_reg_err;
#endif

	mlx5_devlink_set_params_init_values(devlink);

#ifdef HAVE_DEVLINK_PARAM_REGISTER
	err = mlx5_devlink_auxdev_params_register(devlink);
	if (err)
		goto auxdev_reg_err;

	err = mlx5_devlink_max_uc_list_param_register(devlink);
	if (err)
		goto max_uc_list_err;
#endif /* HAVE_DEVLINK_PARAM_REGISTER */

#ifdef HAVE_DEVLINK_TRAP_SUPPORT
	err = mlx5_devlink_traps_register(devlink);
	if (err)
		goto traps_reg_err;
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */

#ifdef HAVE_DEVLINK_PARAMS_PUBLISHED
	devlink_params_publish(devlink);
#endif /* defined(HAVE_DEVLINK_PARAMS_PUBLISHED) && !defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS) */

#ifdef HAVE_DEVLINK_SET_FEATURES
	if (!mlx5_core_is_mp_slave(dev))
		devlink_set_features(devlink, DEVLINK_F_RELOAD);
#endif

	return 0;

#ifdef HAVE_DEVLINK_TRAP_SUPPORT
traps_reg_err:
#ifdef HAVE_DEVLINK_PARAM_REGISTER
	mlx5_devlink_max_uc_list_param_unregister(devlink);
max_uc_list_err:
	mlx5_devlink_auxdev_params_unregister(devlink);
auxdev_reg_err:
#endif /* HAVE_DEVLINK_PARAM_REGISTER */
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */
	devlink_params_unregister(devlink, mlx5_devlink_params,
				  ARRAY_SIZE(mlx5_devlink_params));
#ifndef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
params_reg_err:
	devlink_unregister(devlink);
#endif
#elif defined(CONFIG_MLX5_ESWITCH)
	priv_dev = devlink_priv(devlink);
	eswitch = priv_dev->priv.eswitch;
	if (eswitch && mlx5_esw_vport_match_metadata_supported(eswitch))
		eswitch->flags |= MLX5_ESWITCH_VPORT_MATCH_METADATA;
#endif /* HAVE_DEVLINK_PARAM && (HAVE_DEVLINK_PARAMS_PUBLISHED || HAVE_DEVLINK_REGISTER_GET_1_PARAMS) */
	return err;
}

void mlx5_devlink_unregister(struct devlink *devlink)
{
#if defined(HAVE_DEVLINK_PARAM) && (defined(HAVE_DEVLINK_PARAMS_PUBLISHED) || defined(HAVE_DEVLINK_REGISTER_GET_1_PARAMS))
#ifndef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
	devlink_params_unpublish(devlink);
#endif
#ifdef HAVE_DEVLINK_TRAP_SUPPORT
	mlx5_devlink_traps_unregister(devlink);
#endif /* HAVE_DEVLINK_TRAP_SUPPORT */
#ifdef HAVE_DEVLINK_PARAM_REGISTER
	mlx5_devlink_max_uc_list_param_unregister(devlink);
	mlx5_devlink_auxdev_params_unregister(devlink);
#endif
	devlink_params_unregister(devlink, mlx5_devlink_params,
				  ARRAY_SIZE(mlx5_devlink_params));
#endif /* HAVE_DEVLINK_PARAM && (HAVE_DEVLINK_PARAMS_PUBLISHED || HAVE_DEVLINK_REGISTER_GET_1_PARAMS) */
#ifndef HAVE_DEVLINK_REGISTER_GET_1_PARAMS
	devlink_unregister(devlink);
#endif
}

int
mlx5_devlink_ct_action_on_nat_conns_set(struct devlink *devlink, u32 id,
					struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	dev->mlx5e_res.ct.ct_action_on_nat_conns = ctx->val.vbool;
	return 0;
}

int
mlx5_devlink_ct_action_on_nat_conns_get(struct devlink *devlink, u32 id,
					struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	ctx->val.vbool = dev->mlx5e_res.ct.ct_action_on_nat_conns;
	return 0;
}

int
mlx5_devlink_ct_labels_mapping_set(struct devlink *devlink, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	dev->mlx5e_res.ct.ct_labels_mapping = ctx->val.vbool;
	return 0;
}

int
mlx5_devlink_ct_labels_mapping_get(struct devlink *devlink, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);

	ctx->val.vbool = dev->mlx5e_res.ct.ct_labels_mapping;
	return 0;
}
