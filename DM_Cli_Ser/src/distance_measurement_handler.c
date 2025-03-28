#include <bluetooth/mesh/models.h>
#include <dk_buttons_and_leds.h>
#include <dm.h>
#include <bluetooth/mesh/vnd/dm_common.h>
#include <bluetooth/mesh/vnd/dm_cli.h>
#include <bluetooth/mesh/vnd/dm_srv.h>
#include "distance_measurement_handler.h"
#include "model_handler.h"
#include <string.h>

/*Hardcoded mesh device addresses. Can be found on the nRF Mesh app.*/
#define CLIENT 0x0010 // mesh app: F Client: 10
#define SENSOR 0x0008 // This value is a place holder value for the friend node DM server of the sensor
#define ANCHOR 0x1234 // Value to represent generic anchor
#define ANCHOR1 0x0002 
#define ANCHOR2 0x0003
#define ANCHOR3 0x0007 
#define MAX_REF_ENTRIES 1 //I had this at 1 before but I think this is effecting the multiple distance measurement calls with one button press

static void update_distance(const struct bt_mesh_dm_cli_results *results, struct bt_mesh_msg_ctx *ctx);

/* Global arrays for storing distances between client/sensor and the anchor nodes*/
float CA_Dist[3];
float AS_Dist[3];

/* Client and sensor flags used single hop distance measurement at the end */
bool DM_C_Flags[3];
bool DM_S_Flags[3];

/* Arrays for storing the two closest anchor nodes for the client and sensor */
uint8_t Closest_Anchor_Cli[2];
uint8_t Closest_Anchor_Sen[2];

/* Array to store which anchors the client and sensor have in common*/
uint8_t Anchors_Shared[2];

/* DM server model setup */
static struct bt_mesh_dm_res_entry meas;

static struct bt_mesh_dm_res_entry *measurements[] = {
    &meas,
};

/* Define the server instances */
struct led_ctx distance_server_instances[1] = {
    {.srv = BT_MESH_MODEL_DM_SRV_INIT(&meas, ARRAY_SIZE(measurements))},
};

static uint32_t Node_Addr_Origin(uint32_t Addr)
{
	uint32_t addr_of_start;
	uint32_t start = Addr;

	switch(start){
	case CLIENT: addr_of_start = CLIENT;
				 break;
	case ANCHOR1:
	case ANCHOR2:
	case ANCHOR3:
				 addr_of_start = ANCHOR;
				 break;
	default: addr_of_start = 0xFF;
	
	}
	return addr_of_start;
}

static uint32_t Node_Addr_Dest(uint32_t Addr)
{
	uint32_t addr_of_end;
	uint32_t end = Addr;

	switch(end){
	case ANCHOR1: addr_of_end = 0x01;
				   break;
	case ANCHOR2: addr_of_end = 0x02;
				   break;
	case ANCHOR3: addr_of_end = 0x03;
				   break;
	case SENSOR:  addr_of_end = SENSOR;
	default: addr_of_end = 0xFF;
	}
	return addr_of_end;
}

/* Used to find the two closest anchors to the client and sensor and storing it*/
static void Find_Two_Closest()
{
	// Two closest anchors for client *******************************************************
	float closest_c = 1000.0; // Just really large number that the distance could never be
	for(int i = 0; i < ARRAY_SIZE(CA_Dist); i++){
		if(CA_Dist[i] < closest_c){
			closest_c = CA_Dist[i];
			Closest_Anchor_Cli[0] = i;
		}
	}
	printk("Closest anchor to client: %d\n", Closest_Anchor_Cli[0] + 1);

	float second_closest_c = 1000.0;
	for(int i = 0; i < ARRAY_SIZE(CA_Dist); i++){
		if((CA_Dist[i] < second_closest_c) && (CA_Dist[i] > closest_c)){
			second_closest_c = CA_Dist[i];
			Closest_Anchor_Cli[1] = i;
		}
	}
	printk("Second closest anchor to client: %d\n", Closest_Anchor_Cli[1] + 1);

	// Two closest anchors for sensor ********************************************************
	float closest_s = 1000.0;
	for(int i = 0; i < ARRAY_SIZE(AS_Dist); i++){
		if(AS_Dist[i] < closest_s){
			closest_s = AS_Dist[i];
			Closest_Anchor_Sen[0] = i;
		}
	}
	printk("Closest anchor to sensor: %d\n", Closest_Anchor_Sen[0] + 1);

	float second_closest_s = 1000.0;
	for(int i = 0; i < ARRAY_SIZE(AS_Dist); i++){
		if((AS_Dist[i] < second_closest_s) && (AS_Dist[i] > closest_s)){
			second_closest_s = AS_Dist[i];
			Closest_Anchor_Sen[1] = i;
		}
	}
	printk("Second closest anchor to sensor: %d\n", Closest_Anchor_Sen[1] + 1);
}

static uint8_t FindCommonAnchors()
{
	uint8_t Common_Anchors = 0;
	for(int i = 0; i < ARRAY_SIZE(Closest_Anchor_Cli); i++){
		for(int j = 0; j < ARRAY_SIZE(Closest_Anchor_Sen); j++){
			if(Closest_Anchor_Cli[i] == Closest_Anchor_Sen[j]){
				Anchors_Shared[0] = Closest_Anchor_Cli[i];
				Common_Anchors++;
				break;
			}
		}
	}

	for(int i = 0; i < ARRAY_SIZE(Closest_Anchor_Cli); i++){
		for(int j = 0; j < ARRAY_SIZE(Closest_Anchor_Sen); j++){
			if((Closest_Anchor_Cli[i] == Closest_Anchor_Sen[j]) && (Closest_Anchor_Cli[i] != Anchors_Shared[0])){
				Anchors_Shared[1] = Closest_Anchor_Cli[i];
				Common_Anchors++;
			}
		}
	}
	return Common_Anchors;
}

static void print_result(const struct bt_mesh_dm_cli_results *results, struct bt_mesh_msg_ctx *ctx)
{
	uint32_t start = ctx->addr;
	uint32_t end = results->res->addr;
	uint32_t addr_of_start = Node_Addr_Origin(start);
	uint32_t addr_of_mid = Node_Addr_Dest(start);
	uint32_t addr_of_end = Node_Addr_Dest(end);
	uint8_t Common_Anchors;

	if(addr_of_start == CLIENT)
		printk("Distance from client to anchor%d: %.2f\n", addr_of_end, results->res->res.mcpd.best / 100.0);
	else if(addr_of_start == ANCHOR)
		printk("Distance from anchor%d to sensor: %.2f\n", addr_of_mid, results->res->res.mcpd.best / 100.0);
	else
		printk("Error!\n");
	//printk("Remote address: %x\n", ctx->addr); // This should be the address of the start_reflector device
	//printk("Unicast address of the node distance was measured with: %x\n", results->res->addr); //This should be the address of the start_initiator
	// I'm thinking I could use the two above addresses for checking which distance variable to update. If they are what I think they are.

	update_distance(results, ctx);

	bool client_ready = 1;
	for(int i = 0; i < ARRAY_SIZE(DM_C_Flags); i++){
		if(DM_C_Flags[i] != 1){
			client_ready = 0;
			break;

		}
	}

	bool sensor_ready = 1;
	for(int i = 0; i < ARRAY_SIZE(DM_S_Flags); i++){
		if(DM_S_Flags[i] != 1){
			sensor_ready = 0;
			break;
		}
	}

	if(client_ready && sensor_ready){
		//First get the two closest anchor nodes to the client and sensor
		Find_Two_Closest();

		//Next check if they share any of the nodes. 
		Common_Anchors = FindCommonAnchors();
		printk("The amount of anchors both share: %d\n", Common_Anchors);
		//If they share only one arnchor use it as the reference point for distance estimation.
		if(Common_Anchors == 1){
			printk("Estimated distance from client to server: %.2f\n", CA_Dist[Anchors_Shared[0]] + AS_Dist[Anchors_Shared[0]]);
		}
		else if(Common_Anchors == 2){ //If they share two anchors use the one closest to either the client or sensor
			float CA_Shared_Closest;
			float AS_Shared_Closest;
			uint8_t AnchorToUseCli;
			uint8_t AnchorToUseSen;

			if(CA_Dist[Anchors_Shared[0]] < CA_Dist[Anchors_Shared[1]]){
				CA_Shared_Closest = CA_Dist[Anchors_Shared[0]];
				AnchorToUseCli = Anchors_Shared[0];
			}
			else{
				CA_Shared_Closest = CA_Dist[Anchors_Shared[1]];
				AnchorToUseCli = Anchors_Shared[1];
			}

			if(AS_Dist[Anchors_Shared[0]] < AS_Dist[Anchors_Shared[1]]){
				AS_Shared_Closest = AS_Dist[Anchors_Shared[0]];
				AnchorToUseSen = Anchors_Shared[0];
			}
			else{
				AS_Shared_Closest = AS_Dist[Anchors_Shared[1]];
				AnchorToUseSen = Anchors_Shared[1];
			}

			if(CA_Shared_Closest < AS_Shared_Closest)
				printk("Estimated distance from client to sensor: %.2f\n", CA_Dist[AnchorToUseCli] + AS_Dist[AnchorToUseCli]);
			else
				printk("Estimated distance from client to sensor: %.2f\n", CA_Dist[AnchorToUseSen] + AS_Dist[AnchorToUseSen]);
		}
		else
		printk("Error finding common anchors.\n");
		
		/* Hard reset all flags and arrays used for DM*/
		memset(CA_Dist, 0, sizeof(CA_Dist));
		memset(AS_Dist, 0, sizeof(AS_Dist));

		memset(DM_C_Flags, 0, sizeof(DM_C_Flags));
		memset(DM_S_Flags, 0, sizeof(DM_S_Flags));

		memset(Closest_Anchor_Cli, 0, sizeof(Closest_Anchor_Cli));
		memset(Closest_Anchor_Sen, 0, sizeof(Closest_Anchor_Sen));

		memset(Anchors_Shared, 0, sizeof(Anchors_Shared));
	}
}

static void update_distance(const struct bt_mesh_dm_cli_results *results, struct bt_mesh_msg_ctx *ctx)
{
	uint32_t start = Node_Addr_Origin(ctx->addr);

	if((results->res->quality == DM_QUALITY_OK) && (start == CLIENT)){
		switch(results->res->addr){
			case ANCHOR1: CA_Dist[0] = (float) results->res->res.mcpd.best / 100.0;
						  DM_C_Flags[0] = 1;
						  printk("Distance from client to anchor1 value stored and flag set.\n");
						  break;
			case ANCHOR2: CA_Dist[1] = (float) results->res->res.mcpd.best / 100.0;
						  DM_C_Flags[1] = 1;
						  printk("Distance from client to anchor2 value stored and flag set.\n");
						  break;
			case ANCHOR3: CA_Dist[2] = (float) results->res->res.mcpd.best / 100.0;
			              DM_C_Flags[2] = 1;
						  printk("Distnace from client ot anchor3 value stored and flag set.\n");
						  break;
			default: printk("Error, unknown measurement path from client to anchor.\n");
		}
	}
	else if ((results->res->quality == DM_QUALITY_OK) && (start == ANCHOR)){
		switch (ctx->addr){
			case ANCHOR1: AS_Dist[0] = (float) results->res->res.mcpd.best / 100.0;
					      DM_S_Flags[0] = 1;
						  printk("Distance from anchor1 value stored and flag set.\n");
						  break;
			case ANCHOR2: AS_Dist[1] = (float) results->res->res.mcpd.best / 100.0;
						  DM_S_Flags[1] = 1;
						  printk("Distance from anchor2 value stored and flag set.\n");
						  break;
			case ANCHOR3: AS_Dist[2] = (float) results->res->res.mcpd.best / 100.0;
						  DM_S_Flags[2] = 1;
						  printk("Distance from anchor3 value stored and flag set.\n");
						  break;
			default: printk("Error, unknown measurement path from anchor to sensor.\n");
		}
	}
	else
		printk("Error updating distance measurement.\n");
}


static void cfg_status_handler(struct bt_mesh_dm_cli *cli, 
                struct bt_mesh_msg_ctx *ctx,
                const struct bt_mesh_dm_cli_cfg_status *status);

static void result_handler(struct bt_mesh_dm_cli *cli, 
                struct bt_mesh_msg_ctx *ctx,
                const struct bt_mesh_dm_cli_results *results);

struct bt_mesh_dm_cli_handlers dm_cli_handlers = {
    .cfg_status_handler = cfg_status_handler,
    .result_handler = result_handler,
};

/*DM client response setup*/
struct bt_mesh_dm_res_entry dm_res_entry;
struct bt_mesh_dm_res_entry dm_res_entry2;

/* Define the array of client instances */
struct button distance_client_instances[6] = {
    {.client = BT_MESH_MODEL_DM_CLI_INIT(&dm_res_entry, MAX_REF_ENTRIES, &dm_cli_handlers)},
	{.client = BT_MESH_MODEL_DM_CLI_INIT(&dm_res_entry2, MAX_REF_ENTRIES, &dm_cli_handlers)},
};

static void cfg_status_handler(struct bt_mesh_dm_cli *cli,
                struct bt_mesh_msg_ctx *ctx,
                const struct bt_mesh_dm_cli_cfg_status *status)
{
    // Not going to use bt_mesh_dm_cli_config so leave this blank for now.
}

/*Seems like this function will handle the results from the measurement. So after the measurement data has been calculated after calling  
*bt_mesh_dm_cli_measurement_start the response will pass the calculated data to this function.
*/
static void result_handler(struct bt_mesh_dm_cli *cli,
                struct bt_mesh_msg_ctx *ctx,
                const struct bt_mesh_dm_cli_results *results)
{
    if(results->entry_cnt > 0){
		if(results->res->err_occurred)
			printk("Error while measuring distance.\n");
		else{
			print_result(results, ctx);
		}
	}
}

/*Perform distance meaasurement using DM Mesh API*/
static struct bt_mesh_dm_cfg dm_cfg = {
	.ttl = 10,
	.timeout = 100, //Was 50 before
	.delay = 0 //Was 0 before
};

static struct bt_mesh_dm_cli_start start_param = {
	.reuse_transaction = false,
	.mode = DM_RANGING_MODE_MCPD,
	.addr = ANCHOR1,
	.cfg = &dm_cfg
};

static struct bt_mesh_dm_cli_start start_param2 = {
	.reuse_transaction = false,
	.mode = DM_RANGING_MODE_MCPD,
	.addr = ANCHOR2,
	.cfg = &dm_cfg
};

static struct bt_mesh_dm_cli_start start_param3 = {
	.reuse_transaction = false,
	.mode = DM_RANGING_MODE_MCPD,
	.addr = ANCHOR3,
	.cfg = &dm_cfg
};

static struct bt_mesh_dm_cli_start start_param4 = {
	.reuse_transaction = false,
	.mode = DM_RANGING_MODE_MCPD,
	.addr = SENSOR,
	.cfg = &dm_cfg
};

static void dm_config_n_start(struct bt_mesh_dm_cli *cli,
								struct bt_mesh_msg_ctx *ctx,
								struct bt_mesh_dm_cli_start start_param,
								struct bt_mesh_dm_cli_results *res)
{
	int err = bt_mesh_dm_cli_config(cli, ctx, &dm_cfg, NULL);
	if(err){
		printk("Failed to configure client: %d\n", err);
		return 0;
	}
	else{
		//printk("\nyou are here %p\n", ctx->addr);
		err = bt_mesh_dm_cli_measurement_start(cli, ctx, &start_param, res);
	}
}

void get_dm(struct bt_mesh_dm_cli *cli,
			struct bt_mesh_msg_ctx *ctx,
			struct bt_mesh_dm_cli_results *res,
		    uint8_t mes)
{
	switch(mes){
		case 0:	dm_config_n_start(cli, ctx, start_param, res);
				k_sleep(K_MSEC(300));
				break;
		case 1: dm_config_n_start(cli, ctx, start_param2, res);
				k_sleep(K_MSEC(300));
				break;
		case 2: dm_config_n_start(cli, ctx, start_param3, res);
				k_sleep(K_MSEC(300));
				break;
		case 3:
		case 4:
		case 5:
				dm_config_n_start(cli, ctx, start_param4, res);
				k_sleep(K_MSEC(300));
				break;
		default: printk("Error in get_dm!");
	}

	return 1;
}