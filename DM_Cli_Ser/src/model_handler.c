/**
 * @file
 * @brief Model handler for the distance model client.
 *
 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh/proxy.h>
#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include "model_handler.h"
#include "distance_measurement_handler.h"
#include "Sensor_handler.h"

unsigned int counter = 0;

/* Used for storing the address of the client DM server */
struct bt_mesh_msg_ctx ctx1 = {
	.addr = 0x0010
};

/* Used for storing the address of the anchor1 (left) DM server*/
struct bt_mesh_msg_ctx ctx2 = {
	.addr = 0x0002
};

/* Used for storing the address of the anchor2 (middle) DM server*/
struct bt_mesh_msg_ctx ctx3 = {
	.addr = 0x0003
};

/* Used for storing the address of the anchor3 (right) DM server*/
struct bt_mesh_msg_ctx ctx4 = {
	.addr = 0x0007
};

static void button_handler_cb(uint32_t pressed, uint32_t changed)
{
	if (!bt_mesh_is_provisioned()) {
		return;
	}

	static uint32_t temp_idx;
	static uint32_t motion_threshold_idx;
	int err;
	struct bt_mesh_dm_cli_results res1;
	struct bt_mesh_dm_cli_results res2;

	if(pressed & changed & BIT(0)){
	//for (int i = 0; i < ARRAY_SIZE(distance_client_instances); ++i) {
		//if (!(pressed & changed & BIT(i))) {
			//continue;
		//}
		
		counter = counter % 6;
		switch(counter){
			/* First three cases perform distance measurements from client to anchors */
			case 0: get_dm(&distance_client_instances[0].client, &ctx1, &res1, 0);
					counter++;
					break;
			case 1: get_dm(&distance_client_instances[0].client, &ctx1, &res1, 1);
					counter++;
					break;
			case 2: get_dm(&distance_client_instances[0].client, &ctx1, &res1, 2);
					counter++;
					break;
			/* Last three cases perform distance measurements from anchors to sensor */
			case 3: get_dm(&distance_client_instances[0].client, &ctx2, &res1, 3);
					counter++;
					break; 
			case 4: get_dm(&distance_client_instances[0].client, &ctx3, &res1, 4);
					counter++;
					break;
			case 5: get_dm(&distance_client_instances[0].client, &ctx4, &res1, 5);
					counter++;
					break;
			
			default: printf("Error! Distance measurement request in model_handler not vaild.\n");
		}
	}
	//}
	if(pressed & changed & BIT(1)){
		uint8_t one = 1;
		get_data(one);
	}
	if(pressed & changed & BIT(2)){
		uint8_t two = 2;
		get_data(two);
	}
	if(pressed & changed & BIT(3)){
		uint8_t three = 3;
		get_data(three);
	}

}

/* Set up a repeating delayed work to blink the DK's LEDs when attention is
 * requested.
 */
static struct k_work_delayable attention_blink_work;
static bool attention;

static void attention_blink(struct k_work *work)
{
	static int idx;
	const uint8_t pattern[] = {
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
		BIT(0),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw1))
		BIT(1),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw2))
		BIT(2),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(sw3))
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

static void attention_on(struct bt_mesh_model *mod)
{
    attention = true;
	k_work_reschedule(&attention_blink_work, K_NO_WAIT);
}

static void attention_off(struct bt_mesh_model *mod)
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
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
	BT_MESH_ELEM(1,
		     BT_MESH_MODEL_LIST(
			     BT_MESH_MODEL_CFG_SRV,
			     BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
				 BT_MESH_MODEL_SENSOR_CLI(&sensor_cli)
				 ),
			BT_MESH_MODEL_LIST(
		     BT_MESH_MODEL_DM_CLI(&distance_client_instances[0].client))),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led0))
    BT_MESH_ELEM(2,
                BT_MESH_MODEL_NONE,
                BT_MESH_MODEL_LIST(
                    BT_MESH_MODEL_DM_SRV(&distance_server_instances[0].srv))),
#endif
/*#if DT_NODE_EXISTS(DT_ALIAS(sw1))
	BT_MESH_ELEM(3,
				BT_MESH_MODEL_NONE,
				BT_MESH_MODEL_LIST(
					BT_MESH_MODEL_DM_CLI(&distance_client_instances[1].client))),
#endif*/
};

static const struct bt_mesh_comp comp = {
	.cid = CONFIG_BT_COMPANY_ID,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

const struct bt_mesh_comp *model_handler_init(void)
{
	static struct button_handler button_handler = {
		.cb = button_handler_cb,
	};

	k_work_init_delayable(&attention_blink_work, attention_blink);
	//k_work_init_delayable(&get_data_work, get_data);

	dk_button_handler_add(&button_handler);
	//k_work_schedule(&get_data_work, K_MSEC(GET_DATA_INTERVAL));

	return &comp;
}