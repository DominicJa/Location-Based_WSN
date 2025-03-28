#include <zephyr/bluetooth/bluetooth.h>
#include "Sensor_handler.h"
#include "model_handler.h"

/* My Comment: This function is called anytime sensor sample data is received from a sensor server */
static void sensor_cli_data_cb(struct bt_mesh_sensor_cli *cli,
			       struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_sensor_type *sensor,
			       const struct bt_mesh_sensor_value *value)
{
	if (sensor->id == bt_mesh_sensor_present_dev_op_temp.id) {
		printk("Ambient temperature: %s\n", bt_mesh_sensor_ch_str(value));
	} 
	else if (sensor->id == bt_mesh_sensor_present_amb_rel_humidity.id) {
		printk("Ambient humidity: %s\n", bt_mesh_sensor_ch_str(value));
	}
	else if (sensor->id == bt_mesh_sensor_present_amb_light_level.id){
		printk("Ambient light level: %s\n", bt_mesh_sensor_ch_str(value));
	}
	/*else if (sensor->id ==
		   bt_mesh_sensor_time_since_presence_detected.id) {
		int64_t time_since_presence_detected = 0;
		enum bt_mesh_sensor_value_status status =
			bt_mesh_sensor_value_to_micro(value, &time_since_presence_detected);

		if (status == BT_MESH_SENSOR_VALUE_UNKNOWN) {
			printk("Unknown last presence detected\n");
		} else if (!time_since_presence_detected) {
			printk("Presence detected, or under 1 second since presence detected\n");
		} else {
			printk("%s second(s) since last presence detected\n",
			       bt_mesh_sensor_ch_str(value));
		}
	} else if (sensor->id == bt_mesh_sensor_present_amb_light_level.id) {
		printk("Ambient light level: %s\n", bt_mesh_sensor_ch_str(value));
	}*/
}

/* Any kind of series entry (like bt_mesh_sensor_cli_series_entry_get or bt_mesh_sensor_cli_series_entries_get) received will be handled here. */
/* I don't really know what a series entry is though.*/
/*static void sensor_cli_series_entry_cb(
	struct bt_mesh_sensor_cli *cli, struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_sensor_type *sensor, uint8_t index, uint8_t count,
	const struct bt_mesh_sensor_series_entry *entry)
{
	printk("Relative runtime in %s", bt_mesh_sensor_ch_str(&entry->value[1]));
	printk(" to %s degrees: ", bt_mesh_sensor_ch_str(&entry->value[2]));
	printk("%s percent\n", bt_mesh_sensor_ch_str(&entry->value[0]));
}*/

/* Called when receiving a sensor setting status, which happens when we are calling the getting or setting function api*/
/*static void sensor_cli_setting_status_cb(struct bt_mesh_sensor_cli *cli,
					 struct bt_mesh_msg_ctx *ctx,
					 const struct bt_mesh_sensor_type *sensor,
					 const struct bt_mesh_sensor_setting_status *setting)
{
	printk("Sensor ID: 0x%04x, Setting ID: 0x%04x\n", sensor->id, setting->type->id);
	for (int chan = 0; chan < setting->type->channel_count; chan++) {
		printk("\tChannel %d value: %s\n", chan,
		       bt_mesh_sensor_ch_str(&(setting->value[chan])));
	}
}*/

/* Called when receiving sensor descriptors, which happens when calling bt_mesh_sensor_cli_all_get or bt_mesh_sensor_cli_desc_get. */
/* So it sounds like basic info about what kind of sensor is being used.*/
/*static void sensor_cli_desc_cb(struct bt_mesh_sensor_cli *cli, struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_sensor_info *sensor)
{
	printk("Descriptor of sensor with ID 0x%04x:\n", sensor->id);
	printk("\ttolerance: { positive: %d negative: %d }\n",
	       sensor->descriptor.tolerance.positive, sensor->descriptor.tolerance.negative);
	printk("\tsampling type: %d\n", sensor->descriptor.sampling_type);
}*/

static const struct bt_mesh_sensor_cli_handlers bt_mesh_sensor_cli_handlers = {
	.data = sensor_cli_data_cb,
	//.series_entry = sensor_cli_series_entry_cb,
	//.setting_status = sensor_cli_setting_status_cb,
	//.sensor = sensor_cli_desc_cb,
};

struct bt_mesh_sensor_cli sensor_cli = 
        BT_MESH_SENSOR_CLI_INIT(&bt_mesh_sensor_cli_handlers);

struct k_work_delayable get_data_work;

/* This function is called periodically. In the function it sends requests to the sensor to get specific data*/
void get_data(uint8_t button)
{
	int err;
	uint8_t button_value = button;
	if(button_value == 1){
		err = bt_mesh_sensor_cli_get(
				&sensor_cli, NULL, &bt_mesh_sensor_present_dev_op_temp,
				NULL);
		if (err) {
			printk("Error getting temperature (%d)\n", err);
		}
	}
	if(button_value == 2){
		err = bt_mesh_sensor_cli_get(
				&sensor_cli, NULL, &bt_mesh_sensor_present_amb_rel_humidity,
				NULL);
		if(err){
			printk("Error getting humidity data (%d)\n", err);
		}
	}
	if(button_value == 3){
		err = bt_mesh_sensor_cli_get(
				&sensor_cli, NULL, &bt_mesh_sensor_present_amb_light_level,
				NULL);
		if(err){
			printk("Error getting light data (%d)\n", err);
		}
	}
}

//Not needed get rid of later
const int temp_ranges[][2] = {
        {0, 100},
        {10, 20},
        {22, 30},
        {40, 50},
};

//static const int presence_motion_threshold[] = { 0, 25, 50, 75, 100 };

//Not needed get rid of later
/*int setting_set_int(const struct bt_mesh_sensor_type *sensor_type,
			   const struct bt_mesh_sensor_type *setting_type,
			   const int *values)
{
	struct bt_mesh_sensor_value sensor_vals[CONFIG_BT_MESH_SENSOR_CHANNELS_MAX];
	int err;

	for (int i = 0; i < setting_type->channel_count; i++) {
		err = bt_mesh_sensor_value_from_micro(setting_type->channels[i].format,
						      values[i] * 1000000LL, &sensor_vals[i]);
		if (err) {
			return err;
		}
	}

	return bt_mesh_sensor_cli_setting_set(&sensor_cli, NULL, sensor_type,
					      setting_type, sensor_vals, NULL);
}*/