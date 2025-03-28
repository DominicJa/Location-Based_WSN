#ifndef DISTANCE_MEASUREMENT_HANDLER_H__
#define DISTANCE_MEASUREMENT_HANDLER_H__

#include <zephyr/bluetooth/mesh.h>
#include <dm.h>
#include <bluetooth/mesh/vnd/dm_cli.h>
#include <bluetooth/mesh/vnd/dm_srv.h>

/* Put functions, global structs, and variables here that will be used by model_handler */

/** Context for a single distance measurement client to issue a measurement start with a server. */
struct button {
	/** Generic dm client instance for this switch. */
	struct bt_mesh_dm_cli client;
};

struct led_ctx {
    struct bt_mesh_dm_srv srv;
};

void get_dm(struct bt_mesh_dm_cli *cli,
			struct bt_mesh_msg_ctx *ctx,
			struct bt_mesh_dm_cli_results *res,
			 uint8_t mes);



#endif /* DISTANCE_MEASUREMENT_HANDLER_H__ */