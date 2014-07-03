#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <signal.h>
#include <stdio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "ibeacon.h"

#define EIR_NAME_SHORT 0x08 /* shortened local name */
#define EIR_NAME_COMPLETE 0x09 /* complete local name */
static volatile int signal_received = 0;
static void sigint_handler(int sig)
{
signal_received = sig;
}

static void eir_parse_name(uint8_t *eir, size_t eir_len,
												char *buf, size_t buf_len)
{
	size_t offset;

	offset = 0;
	while (offset < eir_len) {
		uint8_t field_len = eir[0];
		size_t name_len;

		/* Check for the end of EIR */
		if (field_len == 0)
			break;

		if (offset + field_len > eir_len)
			goto failed;

		switch (eir[1]) {
		case EIR_NAME_SHORT:
		case EIR_NAME_COMPLETE:
			name_len = field_len - 1;
			if (name_len > buf_len)
				goto failed;
			memcpy(buf, &eir[2], name_len);
			return;
		}

		offset += field_len + 1;
		eir += field_len + 1;
	}
	
failed:
	snprintf(buf, buf_len, "(unknown)");
}

static int eir_parse_ibeacon_info(uint8_t *data, struct ibeacon_info *info) 
{	
	if ( data[0] == 0x02 && data[1]==0x01 
			&& data[2] == 0x06 && data[3] == 0x1A && data[4] == 0xff ) {
		memcpy(&info->prefix, &data[0], 5);
		memcpy(&info->manufacturer_type, &data[IBEACON_MANUFACTURER_TYPE_S] , IBEACON_MANUFACTURER_TYPE_L);
		memcpy(&info->ad_indictor, &data[IBEACON_AD_INDICATOR_S] , IBEACON_AD_INDICATOR_L);
		memcpy(&info->length, &data[IBEACON_DATA_LENGTH_S] , IBEACON_DATA_LENGTH_L);
		memcpy(&info->uuid, &data[IBEACON_UUID_S] , IBEACON_UUID_L);
		memcpy(&info->major, &data[IBEACON_MAJOR_S] , IBEACON_MAJOR_L);
		memcpy(&info->minor, &data[IBEACON_MINOR_S] , IBEACON_MINOR_L);
		memcpy(&info->tx_power, &data[IBEACON_TX_POWER_S] , IBEACON_TX_POWER_L);
		return 0;
	}
	return 1;
}

static int print_advertising_devices(int dd, uint8_t filter_type, CALLBACK cb_func)
{
	unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
	struct hci_filter nf, of;
	struct sigaction sa;
	struct ibeacon_info iInfo;
	socklen_t olen;
	int len,i,j;

	olen = sizeof(of);
	if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
		printf("Could not get socket options\n");
		return -1;
	}

	hci_filter_clear(&nf);
	hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
	hci_filter_set_event(EVT_LE_META_EVENT, &nf);

	if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
		printf("Could not set socket options\n");
		return -1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_NOCLDSTOP;
	sa.sa_handler = sigint_handler;
	sigaction(SIGINT, &sa, NULL);
    
	while (1) {
		evt_le_meta_event *meta;
		le_advertising_info *info;
		char addr[18];int i;

		while ((len = read(dd, buf, sizeof(buf))) < 0) {
			if (errno == EINTR && signal_received == SIGINT) {
				len = 0;
				goto done;
			}

			if (errno == EAGAIN || errno == EINTR)
			continue;
			goto done;
		}

		ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
		len -= (1 + HCI_EVENT_HDR_SIZE);

		meta = (void *) ptr;

		if (meta->subevent != 0x02)
			goto done;

		/* Ignoring multiple reports */
		info = (le_advertising_info *) (meta->data + 1);
		char name[30];
		memset(name, 0, sizeof(name));
		
		ba2str(&info->bdaddr, addr);
		eir_parse_name(info->data, info->length,
							name, sizeof(name) - 1);
		
		memset(&iInfo, 0, sizeof(iInfo));

		if ( !eir_parse_ibeacon_info(info->data, &iInfo) ) {
			// set mac address
			for ( i=5,j=0; i>=0 ; i--) 
				iInfo.mac[j++] = info->bdaddr.b[i];
			// set rssi
			iInfo.rssi =  *(meta->data + 30+10);
			
			// execute call back function
			cb_func(&iInfo);
			
		}
	}

done:
	setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));

	if (len < 0)
		return -1;

	return 0;
}

int init_bluetooth(char *hci_dev) {
	bdaddr_t ba;
	int dev_id = -1;
	dev_id = hci_devid(hci_dev);
	if (dev_id != -1 && hci_devba(dev_id, &ba) < 0) {
		perror("Device is not available");
		exit(1);
	}
	return dev_id;
}

void start_lescan_loop(int dev_id, CALLBACK cb_func)
{
	int err, opt, dd;
	uint8_t own_type = 0x00;
	uint8_t scan_type = 0x01;
	uint8_t filter_type = 0;
	uint8_t filter_policy = 0x00;
	uint16_t interval = htobs(0x0010);
	uint16_t window = htobs(0x0010);
	uint8_t filter_dup = 0x00;

	if (dev_id < 0)
		dev_id = hci_get_route(NULL);

	dd = hci_open_dev(dev_id);
	if (dd < 0) {
		perror("Could not open device");
		exit(1);
	}

	err = hci_le_set_scan_parameters(dd, scan_type, interval, window,
											own_type, filter_policy, 1000);
	if (err < 0) {
		perror("Set scan parameters failed");
		exit(1);
	}

	err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
	if (err < 0) {
		perror("Enable scan failed");
		exit(1);
	}

	err = print_advertising_devices(dd, filter_type, cb_func);
	if (err < 0) {
		perror("Could not receive advertising events");
	exit(1);
	}

	err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
	if (err < 0) {
		perror("Disable scan failed");
		exit(1);
	}

	hci_close_dev(dd);
}
