#ifndef SENSOR_HANDLER_H__
#define SENSOR_HANDLER_H__

#define GET_DATA_INTERVAL 60000
#define GET_DATA_INTERVAL_QUICK 3000

#include <zephyr/bluetooth/mesh.h>
#include <bluetooth/mesh/models.h>

//extern struct k_work_delayable get_data_work;

extern const int temp_ranges[4][2];

/*int setting_set_int(const struct bt_mesh_sensor_type *sensor_type,
			   const struct bt_mesh_sensor_type *setting_type,
			   const int *values);*/

void get_data(uint8_t buttion);

#endif /* SENSOR_HANDLER_H__ */