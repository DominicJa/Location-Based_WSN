/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/sensor.h>
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/sensor_types.h>
#include <dk_buttons_and_leds.h>
#include <float.h>
#include "model_handler.h"

//Checking which board is being used and picking the correct sensor to use
#if DT_NODE_HAS_STATUS(DT_NODELABEL(bme680), okay)
/** Thingy53 */
#define SENSOR_NODE DT_NODELABEL(bme680)
#define SENSOR_NODE2 DT_NODELABEL(bh1749)
#define SENSOR_DATA_TYPE SENSOR_CHAN_AMBIENT_TEMP
#else
#error "Unsupported board!"
#endif

//Settings and resolution for temp (I think)
#define TEMP_INIT(_val) { .format = &bt_mesh_sensor_format_temp, .raw = {      \
	FIELD_GET(GENMASK(7, 0), (_val) * 100),                                \
	FIELD_GET(GENMASK(15, 8), (_val) * 100)                                \
}}

//Resolution of column of data (I think)
#define COL_INIT(_start, _width) { TEMP_INIT(_start), TEMP_INIT(_width) }

//Settings and resolution for Illuminance (I think)
#define ILLUMINANCE_INIT_MILLIS(_val)                                          \
{                                                                              \
	.format = &bt_mesh_sensor_format_illuminance,                          \
	.raw = {                                                               \
		FIELD_GET(GENMASK(7, 0), (_val / 10)),                         \
		FIELD_GET(GENMASK(15, 8), (_val / 10)),                        \
		FIELD_GET(GENMASK(23, 16), (_val / 10)),                       \
	}                                                                      \
}

/* The columns (temperature ranges) for relative
 * runtime in a chip temperature
 */
static const struct bt_mesh_sensor_column columns[] = {
	COL_INIT(0, 20),
	COL_INIT(20, 5),
	COL_INIT(25, 5),
	COL_INIT(30, 70)
};

struct sensor_value_range {
	struct bt_mesh_sensor_value start;
	struct bt_mesh_sensor_value end;
};

#define DEFAULT_TEMP_RANGE_LOW 0
#define DEFAULT_TEMP_RANGE_HIGH 100
/* Range limiting the reported values of the chip temperature */
static struct sensor_value_range temp_range = {
	TEMP_INIT(DEFAULT_TEMP_RANGE_LOW),
	TEMP_INIT(DEFAULT_TEMP_RANGE_HIGH),
};

static const struct device *dev = DEVICE_DT_GET(SENSOR_NODE);
static const struct device *dev2 = DEVICE_DT_GET(SENSOR_NODE2);
static uint32_t tot_temp_samps;
static uint32_t col_samps[ARRAY_SIZE(columns)];
static uint32_t outside_temp_range;
static struct bt_mesh_sensor_value pres_mot_thres = {
	.format = &bt_mesh_sensor_format_percentage_8,
	/* Initialize to "Value is not known" encoded as 0xff. */
	.raw = { 0xff }
};
static struct bt_mesh_sensor_value amb_light_level_ref = ILLUMINANCE_INIT_MILLIS(0);
static float amb_light_level_gain = 1.0;
/* Using a dummy ambient light value because we do not have a real ambient light sensor. (Maybe not the nrf52 dks but the thingy 53 does :^) )*/
static float dummy_ambient_light_value;

static bool pres_detect;
static uint32_t prev_detect;

#if IS_ENABLED(CONFIG_BT_MESH_NLC_PERF_CONF)
static const uint8_t cmp2_elem_offset1[1] = { 0 };
static const uint8_t cmp2_elem_offset2[1] = { 1 };

// ???
static const struct bt_mesh_comp2_record comp_rec[2] = {
	{
	.id = BT_MESH_NLC_PROFILE_ID_AMBIENT_LIGHT_SENSOR,
	.version.x = 1,
	.version.y = 0,
	.version.z = 0,
	.elem_offset_cnt = 1,
	.elem_offset = cmp2_elem_offset1,
	.data_len = 0
	},
	{
	.id = BT_MESH_NLC_PROFILE_ID_OCCUPANCY_SENSOR,
	.version.x = 1,
	.version.y = 0,
	.version.z = 0,
	.elem_offset_cnt = 1,
	.elem_offset = cmp2_elem_offset2,
	.data_len = 0
	}
};

static const struct bt_mesh_comp2 comp_p2 = {
	.record_cnt = 2,
	.record = comp_rec
};
#endif
static int chip_temp_get(struct bt_mesh_sensor_srv *srv,
			 struct bt_mesh_sensor *sensor,
			 struct bt_mesh_msg_ctx *ctx,
			 struct bt_mesh_sensor_value *rsp)
{
	struct sensor_value channel_val;
	int err;

	sensor_sample_fetch(dev);

	err = sensor_channel_get(dev, SENSOR_DATA_TYPE, &channel_val);
	if (err) {
		printk("Error getting temperature sensor data (%d)\n", err);
	}
	err = bt_mesh_sensor_value_from_sensor_value(
		sensor->type->channels[0].format, &channel_val, rsp);
	if (err) {
		printk("Error encoding temperature sensor data (%d)\n", err);
	}

	if (!BT_MESH_SENSOR_VALUE_IN_RANGE(rsp, &temp_range.start, &temp_range.end)) {
		outside_temp_range++;
	}

	for (int i = 0; i < ARRAY_SIZE(columns); ++i) {
		if (bt_mesh_sensor_value_in_column(rsp, &columns[i])) {
			col_samps[i]++;
			break;
		}
	}

	tot_temp_samps++;

	return err;
}

/* Tolerance is based on the nRF52832's temperature sensor's accuracy and range (5/125 = 4%). */
// 125 comes from the total temp range of the 52832 sensor and the 5 comes from the +-5 accuracy of the sensor
static const struct bt_mesh_sensor_descriptor chip_temp_descriptor = {
	.tolerance = {
		.negative = BT_MESH_SENSOR_TOLERANCE_ENCODE(4),
		.positive = BT_MESH_SENSOR_TOLERANCE_ENCODE(4),
	},
	.sampling_type = BT_MESH_SENSOR_SAMPLING_INSTANTANEOUS,
};

static void chip_temp_range_get(struct bt_mesh_sensor_srv *srv, struct bt_mesh_sensor *sensor,
				const struct bt_mesh_sensor_setting *setting,
				struct bt_mesh_msg_ctx *ctx, struct bt_mesh_sensor_value *rsp)
{
	rsp[0] = temp_range.start;
	rsp[1] = temp_range.end;
	printk("Temperature sensor lower limit: %s\n", bt_mesh_sensor_ch_str(&rsp[0]));
	printk("Temperature sensor upper limit: %s\n", bt_mesh_sensor_ch_str(&rsp[1]));
}

static int chip_temp_range_set(struct bt_mesh_sensor_srv *srv, struct bt_mesh_sensor *sensor,
			       const struct bt_mesh_sensor_setting *setting,
			       struct bt_mesh_msg_ctx *ctx,
			       const struct bt_mesh_sensor_value *value)
{
	temp_range.start = value[0];
	temp_range.end = value[1];
	printk("Temperature sensor lower limit: %s\n", bt_mesh_sensor_ch_str(&value[0]));
	printk("Temperature sensor upper limit: %s\n", bt_mesh_sensor_ch_str(&value[1]));

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		int err;

		err = settings_save_one("temp/range", &temp_range, sizeof(temp_range));
		if (err) {
			printk("Error storing setting (%d)\n", err);
		} else {
			printk("Stored setting\n");
		}
	}
	return 0;
}

static struct bt_mesh_sensor_setting chip_temp_setting[] = { {
	.type = &bt_mesh_sensor_dev_op_temp_range_spec,
	.get = chip_temp_range_get,
	.set = chip_temp_range_set,
} };

static int chip_temp_range_settings_restore(const char *name, size_t len, settings_read_cb read_cb,
					    void *cb_arg)
{
	const char *next;
	int rc;

	if (!(settings_name_steq(name, "range", &next) && !next)) {
		return -ENOENT;
	}

	if (len != sizeof(temp_range)) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, &temp_range, sizeof(temp_range));
	if (rc < 0) {
		return rc;
	}

	printk("Restored temperature range setting\n");
	return 0;
}

struct settings_handler temp_range_conf = { .name = "temp",
					    .h_set = chip_temp_range_settings_restore };

static struct bt_mesh_sensor chip_temp = {
	.type = &bt_mesh_sensor_present_dev_op_temp,
	.get = chip_temp_get,
	.descriptor = &chip_temp_descriptor,
	.settings = {
		.list = (const struct bt_mesh_sensor_setting *)&chip_temp_setting,
		.count = ARRAY_SIZE(chip_temp_setting),
	},
};

static int chip_humidity_get(struct bt_mesh_sensor_srv *srv,
			 struct bt_mesh_sensor *sensor,
			 struct bt_mesh_msg_ctx *ctx,
			 struct bt_mesh_sensor_value *rsp)
{
	struct sensor_value channel_val;
	int err;

	sensor_sample_fetch(dev);

	err = sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &channel_val);
	if (err) {
		printk("Error getting humidity sensor data (%d)\n", err);
	}
	err = bt_mesh_sensor_value_from_sensor_value(
		sensor->type->channels[0].format, &channel_val, rsp);
	if (err) {
		printk("Error encoding humidity sensor data (%d)\n", err);
	}

	/*if (!BT_MESH_SENSOR_VALUE_IN_RANGE(rsp, &temp_range.start, &temp_range.end)) {
		outside_temp_range++;
	}

	for (int i = 0; i < ARRAY_SIZE(columns); ++i) {
		if (bt_mesh_sensor_value_in_column(rsp, &columns[i])) {
			col_samps[i]++;
			break;
		}
	}

	tot_temp_samps++;*/

	return err;
}

static struct bt_mesh_sensor chip_humidity = {
	.type = &bt_mesh_sensor_present_amb_rel_humidity,
	.get = chip_humidity_get,
	//.descriptor = &chip_temp_descriptor,
	//.settings = {
		//.list = (const struct bt_mesh_sensor_setting *)&chip_temp_setting,
		//.count = ARRAY_SIZE(chip_temp_setting),
	//},
};

static int relative_runtime_in_chip_temp_series_get(struct bt_mesh_sensor_srv *srv,
	struct bt_mesh_sensor *sensor,
	struct bt_mesh_msg_ctx *ctx,
	uint32_t column_index,
	struct bt_mesh_sensor_value *value)
{
	int err;

	if (tot_temp_samps) {
		int64_t percent_micros =
			(100 * 1000000LL * col_samps[column_index]) / tot_temp_samps;

		err = bt_mesh_sensor_value_from_micro(sensor->type->channels[0].format,
						      percent_micros, &value[0]);
		if (err) {
			printk("Error encoding relative runtime in chip temp series (%d)\n", err);
		}
	} else {
		err = bt_mesh_sensor_value_from_micro(sensor->type->channels[0].format,
						      0, &value[0]);
		if (err) {
			printk("Error encoding relative runtime in chip temp series (%d)\n", err);
		}
	}

	value[1] = columns[column_index].start;

	int64_t start_micro, width_micro;

	(void)bt_mesh_sensor_value_to_micro(&columns[column_index].start, &start_micro);
	(void)bt_mesh_sensor_value_to_micro(&columns[column_index].width, &width_micro);
	err = bt_mesh_sensor_value_from_micro(columns[column_index].start.format,
					      start_micro + width_micro, &value[2]);
	if (err) {
		printk("Error encoding column end (%d)\n", err);
	}

	return 0;
}

static int relative_runtime_in_chip_temp_get(struct bt_mesh_sensor_srv *srv,
					     struct bt_mesh_sensor *sensor,
					     struct bt_mesh_msg_ctx *ctx,
					     struct bt_mesh_sensor_value *rsp)
{
	int err;

	if (tot_temp_samps) {
		int64_t percent_micros =
			(100 * 1000000LL * (tot_temp_samps - outside_temp_range)) / tot_temp_samps;

		err = bt_mesh_sensor_value_from_micro(sensor->type->channels[0].format,
						      percent_micros, &rsp[0]);
		if (err) {
			printk("Error encoding relative runtime in chip temp (%d)\n", err);
		}
	} else {
		err = bt_mesh_sensor_value_from_micro(sensor->type->channels[0].format,
						      100 * 1000000LL, &rsp[0]);
		if (err) {
			printk("Error encoding relative runtime in chip temp series (%d)\n", err);
		}
	}

	rsp[1] = temp_range.start;
	rsp[2] = temp_range.end;

	return 0;
}

static struct bt_mesh_sensor rel_chip_temp_runtime = {
	.type = &bt_mesh_sensor_rel_runtime_in_a_dev_op_temp_range,
	.get = relative_runtime_in_chip_temp_get,
	.series = {
		columns,
		ARRAY_SIZE(columns),
		relative_runtime_in_chip_temp_series_get,
	},
};

static void presence_motion_threshold_get(struct bt_mesh_sensor_srv *srv,
	struct bt_mesh_sensor *sensor,
	const struct bt_mesh_sensor_setting *setting,
	struct bt_mesh_msg_ctx *ctx,
	struct bt_mesh_sensor_value *rsp)
{
	int64_t mot_thres_micro;
	enum bt_mesh_sensor_value_status status;

	rsp[0] = pres_mot_thres;
	status = bt_mesh_sensor_value_to_micro(&rsp[0], &mot_thres_micro);
	if (bt_mesh_sensor_value_status_is_numeric(status)) {
		printk("Presence motion threshold: %s [%d ms]\n", bt_mesh_sensor_ch_str(rsp),
		       (uint16_t)(mot_thres_micro / 10000));
	} else {
		printk("Presence motion threshold: %s\n", bt_mesh_sensor_ch_str(rsp));
	}
}

static int presence_motion_threshold_set(struct bt_mesh_sensor_srv *srv,
	struct bt_mesh_sensor *sensor,
	const struct bt_mesh_sensor_setting *setting,
	struct bt_mesh_msg_ctx *ctx,
	const struct bt_mesh_sensor_value *value)
{
	int64_t mot_thres_micro;
	enum bt_mesh_sensor_value_status status;

	pres_mot_thres = value[0];
	status = bt_mesh_sensor_value_to_micro(&value[0], &mot_thres_micro);
	if (bt_mesh_sensor_value_status_is_numeric(status)) {
		printk("Presence motion threshold: %s [%d ms]\n", bt_mesh_sensor_ch_str(value),
		       (uint16_t)(mot_thres_micro / 10000));
	} else {
		printk("Presence motion threshold: %s\n", bt_mesh_sensor_ch_str(value));
	}

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		int err;

		err = settings_save_one("presence/motion_threshold",
					&pres_mot_thres, sizeof(pres_mot_thres));
		if (err) {
			printk("Error storing setting (%d)\n", err);
		} else {
			printk("Stored setting\n");
		}
	}
	return 0;
}

static struct bt_mesh_sensor_setting presence_motion_threshold_setting[] = { {
	.type = &bt_mesh_sensor_motion_threshold,
	.get = presence_motion_threshold_get,
	.set = presence_motion_threshold_set,
} };

static int presence_motion_threshold_settings_restore(const char *name,
	size_t len,
	settings_read_cb read_cb,
	void *cb_arg)
{
	const char *next;
	int rc;

	if (!(settings_name_steq(name, "motion_threshold", &next) && !next)) {
		return -ENOENT;
	}

	if (len != sizeof(pres_mot_thres)) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, &pres_mot_thres, sizeof(pres_mot_thres));
	if (rc < 0) {
		return rc;
	}

	printk("Restored presence motion threshold setting\n");
	return 0;
}

struct settings_handler presence_motion_threshold_conf = {
	.name = "presence",
	.h_set = presence_motion_threshold_settings_restore
};

static int presence_detected_get(struct bt_mesh_sensor_srv *srv,
				 struct bt_mesh_sensor *sensor,
				 struct bt_mesh_msg_ctx *ctx,
				 struct bt_mesh_sensor_value *rsp)
{
	int err = bt_mesh_sensor_value_from_micro(sensor->type->channels[0].format,
						  pres_detect * 1000000LL, rsp);

	if (err) {
		printk("Error encoding presence detected (%d)", err);
	}
	return err;
};

static struct bt_mesh_sensor presence_sensor = {
	.type = &bt_mesh_sensor_presence_detected,
	.get = presence_detected_get,
	.settings = {
		.list = (const struct bt_mesh_sensor_setting *)&presence_motion_threshold_setting,
		.count = ARRAY_SIZE(presence_motion_threshold_setting),
	},
};

static int time_since_presence_detected_get(struct bt_mesh_sensor_srv *srv,
					struct bt_mesh_sensor *sensor,
					struct bt_mesh_msg_ctx *ctx,
					struct bt_mesh_sensor_value *rsp)
{
	int err;
	const struct bt_mesh_sensor_format *format = sensor->type->channels[0].format;

	if (pres_detect) {
		err = bt_mesh_sensor_value_from_micro(format, 0, rsp);
	} else if (prev_detect) {
		int64_t micro_since_detect =
			(k_uptime_get() - prev_detect) * USEC_PER_MSEC;

		err = bt_mesh_sensor_value_from_micro(format, micro_since_detect, rsp);
		if (err == -ERANGE) {
			/* Ignore range error and respond with clamped value */
			return 0;
		}
	} else {
		/* Before first detection, the time since last detection is unknown. Returning
		 * unknown value until a detection is done.
		 */
		err = bt_mesh_sensor_value_from_special_status(
			format, BT_MESH_SENSOR_VALUE_UNKNOWN, rsp);
	}

	if (err) {
		printk("Error encoding time since presence detected (%d)", err);
	}
	return err;
}

static struct bt_mesh_sensor time_since_presence_detected = {
	.type = &bt_mesh_sensor_time_since_presence_detected,
	.get = time_since_presence_detected_get,
};

static void amb_light_level_gain_get(struct bt_mesh_sensor_srv *srv,
				     struct bt_mesh_sensor *sensor,
				     const struct bt_mesh_sensor_setting *setting,
				     struct bt_mesh_msg_ctx *ctx,
				     struct bt_mesh_sensor_value *rsp)
{
	int err = bt_mesh_sensor_value_from_float(setting->type->channels[0].format,
						  amb_light_level_gain, rsp);

	if (err) {
		printk("Error encoding ambient light level gain (%d)\n", err);
	} else {
		printk("Ambient light level gain: %s\n", bt_mesh_sensor_ch_str(rsp));
	}
};

static void amb_light_level_gain_store(float gain)
{
	amb_light_level_gain = gain;

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		int err;

		err = settings_save_one("amb_light_level/gain",
					&amb_light_level_gain, sizeof(amb_light_level_gain));
		if (err) {
			printk("Error storing setting (%d)\n", err);
		} else {
			printk("Stored setting\n");
		}
	}
}

static int amb_light_level_gain_set(struct bt_mesh_sensor_srv *srv,
				    struct bt_mesh_sensor *sensor,
				    const struct bt_mesh_sensor_setting *setting,
				    struct bt_mesh_msg_ctx *ctx,
				    const struct bt_mesh_sensor_value *value)
{
	float value_f;

	/* Ignore status; type is float and can always be decoded to a float. */
	(void)bt_mesh_sensor_value_to_float(value, &value_f);

	amb_light_level_gain_store(value_f);
	printk("Ambient light level gain: %s\n", bt_mesh_sensor_ch_str(value));

	return 0;
}

static int amb_light_level_ref_set(struct bt_mesh_sensor_srv *srv,
				   struct bt_mesh_sensor *sensor,
				   const struct bt_mesh_sensor_setting *setting,
				   struct bt_mesh_msg_ctx *ctx,
				   const struct bt_mesh_sensor_value *value)
{
	amb_light_level_ref = value[0];

	float ref_float;
	enum bt_mesh_sensor_value_status status = bt_mesh_sensor_value_to_float(
		value, &ref_float);

	if (!bt_mesh_sensor_value_status_is_numeric(status)) {
		/* Format can encode "Value is not known",
		 * handle by keeping the gain unchanged.
		 */
		printk("Ambient light level ref set to %s, gain is not modified\n",
		       bt_mesh_sensor_ch_str(value));
		return 0;
	}

	/* When using the a real ambient light sensor the sensor value should be
	 * read and used instead of the dummy value.
	 */
	if (dummy_ambient_light_value > 0.0) {
		amb_light_level_gain_store(ref_float / dummy_ambient_light_value);
	} else {
		amb_light_level_gain_store(FLT_MAX);
	}

	printk("Ambient light level ref(%s) ", bt_mesh_sensor_ch_str(value));
	printk("gain(%f)\n", amb_light_level_gain);

	return 0;
}

static void amb_light_level_ref_get(struct bt_mesh_sensor_srv *srv,
				     struct bt_mesh_sensor *sensor,
				     const struct bt_mesh_sensor_setting *setting,
				     struct bt_mesh_msg_ctx *ctx,
				     struct bt_mesh_sensor_value *rsp)
{
	rsp[0] = amb_light_level_ref;
	printk("Ambient light level ref: %s\n", bt_mesh_sensor_ch_str(rsp));
};

static struct bt_mesh_sensor_setting amb_light_level_setting[] = {
	{
		.type = &bt_mesh_sensor_gain,
		.get = amb_light_level_gain_get,
		.set = amb_light_level_gain_set,
	},
	{
		.type = &bt_mesh_sensor_present_amb_light_level,
		.get = amb_light_level_ref_get,
		.set = amb_light_level_ref_set,
	},
};

static int amb_light_level_gain_settings_restore(const char *name,
						 size_t len,
						 settings_read_cb read_cb,
						 void *cb_arg)
{
	const char *next;
	int rc;

	if (!(settings_name_steq(name, "gain", &next) && !next)) {
		return -ENOENT;
	}

	if (len != sizeof(amb_light_level_gain)) {
		return -EINVAL;
	}

	rc = read_cb(cb_arg, &amb_light_level_gain, sizeof(amb_light_level_gain));
	if (rc < 0) {
		return rc;
	}

	printk("Restored ambient light level gain setting\n");
	return 0;
}

struct settings_handler amb_light_level_gain_conf = {
	.name = "amb_light_level",
	.h_set = amb_light_level_gain_settings_restore
};

static int amb_light_level_get(struct bt_mesh_sensor_srv *srv,
			       struct bt_mesh_sensor *sensor,
			       struct bt_mesh_msg_ctx *ctx,
			       struct bt_mesh_sensor_value *rsp)
{
	//int err;

	/* Report ambient light as dummy value, and changing it by pressing
	 * a button. The logic and hardware for measuring the actual ambient
	 * light usage of the device should be implemented here.
	 */
	//float reported_value = amb_light_level_gain * dummy_ambient_light_value;
	struct sensor_value channel_val;
	int err;

	sensor_sample_fetch(dev2);

	err = sensor_channel_get(dev2, SENSOR_CHAN_GREEN, &channel_val);
	if (err) {
		printk("Error getting humidity sensor data (%d)\n", err);
	}
	err = bt_mesh_sensor_value_from_sensor_value(
		sensor->type->channels[0].format, &channel_val, rsp);
	if (err) {
		printk("Error encoding humidity sensor data (%d)\n", err);
	}


	/*err = bt_mesh_sensor_value_from_float(sensor->type->channels[0].format,
					      reported_value, rsp);
	if (err && err != -ERANGE) {
		printk("Error encoding ambient light level sensor data (%d)\n", err);
		return err;
	}*/

	//return 0;
	return err;
}

static const struct bt_mesh_sensor_descriptor present_amb_light_desc = {
	.tolerance = {
		.negative = BT_MESH_SENSOR_TOLERANCE_ENCODE(0),
		.positive = BT_MESH_SENSOR_TOLERANCE_ENCODE(0),
	},
	.sampling_type = BT_MESH_SENSOR_SAMPLING_UNSPECIFIED,
	.period = 0,
	.update_interval = 0,
};


static struct bt_mesh_sensor present_amb_light_level = {
	.type = &bt_mesh_sensor_present_amb_light_level,
	.get = amb_light_level_get,
	.descriptor = &present_amb_light_desc,
	.settings = {
		.list = (const struct bt_mesh_sensor_setting *)&amb_light_level_setting,
		.count = ARRAY_SIZE(amb_light_level_setting),
	},
};

static struct bt_mesh_sensor *const ambient_light_sensor[] = {
	&present_amb_light_level,
};

static struct bt_mesh_sensor *const occupancy_sensor[] = {
	&presence_sensor,
	&time_since_presence_detected,
};

static struct bt_mesh_sensor *const chip_temp_sensor[] = {
	&chip_temp,
	&rel_chip_temp_runtime,
};

static struct bt_mesh_sensor *const chip_humidity_sensor[] = {
	&chip_humidity,
};

static struct bt_mesh_sensor_srv ambient_light_sensor_srv =
	BT_MESH_SENSOR_SRV_INIT(ambient_light_sensor, ARRAY_SIZE(ambient_light_sensor));
static struct bt_mesh_sensor_srv occupancy_sensor_srv =
	BT_MESH_SENSOR_SRV_INIT(occupancy_sensor, ARRAY_SIZE(occupancy_sensor));
static struct bt_mesh_sensor_srv chip_temp_sensor_srv =
	BT_MESH_SENSOR_SRV_INIT(chip_temp_sensor, ARRAY_SIZE(chip_temp_sensor));
static struct bt_mesh_sensor_srv chip_humidity_sensor_srv =
	BT_MESH_SENSOR_SRV_INIT(chip_humidity_sensor, ARRAY_SIZE(chip_humidity_sensor));

static struct k_work_delayable presence_detected_work;

#define BOOLEAN_INIT(_bool) { .format = &bt_mesh_sensor_format_boolean, .raw = { (_bool) } }

static void presence_detected(struct k_work *work)
{
	int err;
	struct bt_mesh_sensor_value val = BOOLEAN_INIT(true);

	err = bt_mesh_sensor_srv_pub(&occupancy_sensor_srv, NULL, &presence_sensor, &val);

	if (err) {
		printk("Error publishing end of presence (%d)\n", err);
	}

	pres_detect = 1;
}

static const double dummy_amb_light_values[] = {
	0.01,
	100.00,
	200.00,
	500.00,
	750.00,
	1000.00,
	10000.00,
	167772.13,
};

static void button_handler_cb(uint32_t pressed, uint32_t changed)
{
	if (!bt_mesh_is_provisioned()) {
		return;
	}

	if (pressed & changed & BIT(0)) {
		int64_t thres_micros;
		enum bt_mesh_sensor_value_status status;

		status = bt_mesh_sensor_value_to_micro(&pres_mot_thres, &thres_micros);
		if (bt_mesh_sensor_value_status_is_numeric(status)) {
			k_work_reschedule(&presence_detected_work, K_MSEC(thres_micros / 10000));
		} else {
			/* Value is not known, register presence immediately */
			k_work_reschedule(&presence_detected_work, K_NO_WAIT);
		}
	}

	if ((!pressed) & changed & BIT(0)) {
		if (!pres_detect) {
			k_work_cancel_delayable(&presence_detected_work);
		} else {
			int err;
			struct bt_mesh_sensor_value val = BOOLEAN_INIT(false);

			err = bt_mesh_sensor_srv_pub(&occupancy_sensor_srv, NULL,
						&presence_sensor, &val);

			if (err) {
				printk("Error publishing presence (%d)\n", err);
			}

			pres_detect = 0;
			prev_detect = k_uptime_get_32();
		}
	}

	if (pressed & changed & BIT(1)) {
		int err;
		static int amb_light_idx;
		struct bt_mesh_sensor_value val;

		dummy_ambient_light_value = dummy_amb_light_values[amb_light_idx++];
		amb_light_idx = amb_light_idx % ARRAY_SIZE(dummy_amb_light_values);

		err = bt_mesh_sensor_value_from_float(
			present_amb_light_level.type->channels[0].format,
			dummy_ambient_light_value, &val);
		if (err) {
			printk("Error getting ambient light level sensor data (%d)\n", err);
		}

		err = bt_mesh_sensor_srv_pub(&ambient_light_sensor_srv, NULL,
					     &present_amb_light_level, &val);
		if (err) {
			printk("Error publishing present ambient light level (%d)\n", err);
		}
	}
}

static struct button_handler button_handler = {
	.cb = button_handler_cb,
};

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led3))
		BIT(3),
#endif
	};

	if (attention) {
		dk_set_leds(pattern[idx++ % ARRAY_SIZE(pattern)]);
		k_work_reschedule(&attention_blink_work, K_MSEC(30));
	} else {
		dk_set_leds(DK_NO_LEDS_MSK);
	}
}

static void attention_on(const struct bt_mesh_model *mod)
{
	attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(const struct bt_mesh_model *mod)
{
	/* Will stop rescheduling blink timer */
	attention = false;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.attn_on = attention_on,
	.attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(1,
		     BT_MESH_MODEL_LIST(BT_MESH_MODEL_CFG_SRV,
					BT_MESH_MODEL_HEALTH_SRV(&health_srv,
								 &health_pub),
					BT_MESH_MODEL_SENSOR_SRV(&ambient_light_sensor_srv)),
		     BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(2,
		     BT_MESH_MODEL_LIST(BT_MESH_MODEL_SENSOR_SRV(&chip_humidity_sensor_srv)),
		     BT_MESH_MODEL_NONE),
	BT_MESH_ELEM(3,
		     BT_MESH_MODEL_LIST(BT_MESH_MODEL_SENSOR_SRV(&chip_temp_sensor_srv)),
		     BT_MESH_MODEL_NONE),
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
#if IS_ENABLED(CONFIG_BT_MESH_NLC_PERF_CONF)
	if (bt_mesh_comp2_register(&comp_p2)) {
		printf("Failed to register comp2\n");
	}
#endif

	k_work_init_delayable(&attention_blink_work, attention_blink);
	//k_work_init_delayable(&presence_detected_work, presence_detected);

	if (!device_is_ready(dev)) {
		printk("Temperature sensor not ready\n");
	} else {
		printk("Temperature sensor (%s) initiated\n", dev->name);
	}

	dk_button_handler_add(&button_handler);

	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		settings_subsys_init();
		settings_register(&temp_range_conf);
		settings_register(&presence_motion_threshold_conf);
		settings_register(&amb_light_level_gain_conf);
	}

	return &comp;
}
