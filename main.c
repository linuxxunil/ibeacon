#include <stdio.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "ibeacon.h"

void get_ibeacon_info(struct ibeacon_info *info) 
{
/*
	int i=0;
	
	printf("mac=");
	for(i=0; i<6; i++)
		printf("%02x ",info->mac[i]);
	printf("\n");
	
	printf("manufacturer_type=%02X %02X\n",info->manufacturer_type[0],info->manufacturer_type[1]);
	printf("ad_indictor=%02X\n",info->ad_indictor);
	printf("length=%02X\n",info->length);
	printf("uuid=");
	for(i=0; i<IBEACON_UUID_L; i++)
		printf("%02X ",info->uuid[i]);
	printf("\n");
	printf("major=%02X %02X\n",info->major[0],info->major[1]);
	printf("minor=%02X %02X\n",info->minor[0],info->minor[1]);
	printf("tx_power=%02X\n",info->tx_power);
	printf("rssi=%02X\n",info->rssi);
	*/
	
	int tx_power = 0;
	int rssi = 0;
	if ( info->rssi > 128 ) rssi = 255 - info->rssi;
	else rssi = info->rssi;
	
	if ( info->tx_power > 128 ) tx_power = 255 - info->tx_power;
	else tx_power = info->tx_power;
	
	
	printf("%f\n",rssi*(1.0/tx_power));
}

int main(int argc, char *argv[])
{
	int dev_id = -1;
	
	char *hci_dev = "hci1";

	dev_id = init_bluetooth(hci_dev);
	
	start_lescan_loop(dev_id,get_ibeacon_info);

	return 0;
}