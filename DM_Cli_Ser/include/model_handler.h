/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file
 * @brief Model handler
 */

#ifndef MODEL_HANDLER_H__
#define MODEL_HANDLER_H__

#include <zephyr/bluetooth/mesh.h>
#include "distance_measurement_handler.h"

/*#ifdef __cplusplus
extern "C" {
#endif*/

const struct bt_mesh_comp *model_handler_init(void);
extern struct button distance_client_instances[6];
extern struct led_ctx distance_server_instances[1];
extern struct bt_mesh_sensor_cli sensor_cli;

/*#ifdef __cplusplus
}
#endif*/

#endif /* MODEL_HANDLER_H__ */