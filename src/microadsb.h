#ifndef __MODES_MICROADSB_H__
#define __MODES_MICROADSB_H__

/* for the microADS-B v1 device with SPRUT firmware 5 */
#define MADSB_KNOWNVERSION5 "#00-00-05-04"
/* for the microADS-B v1 device with SPRUT firmware 6 */
#define MADSB_KNOWNVERSION6 "#00-00-06-04"
/* for the microADS-B v2 device with SPRUT firmware 8 */
#define MADSB_KNOWNVERSION8 "#00-00-08-04"
/* see user.[ch] in SPRUT firmware source for commands info */
#define MADSB_CMD_READ_VERSION   0x00 /* no args */
#define MADSB_CMD_SET_MODE       0x43 /* takes one byte, see MADSB_MODE_* */
#define MADSB_CMD_RESET          0xff /* no args */

#define MADSB_MODE_ALL           0x02 /* send all demodulated squitters, in *...; format */
#define MADSB_MODE_ADSB          0x03 /* send only ADS-B (DF=17/18/19) squitters, in *...; format */
#define MADSB_MODE_ADSB_CRC      0x04 /* send only ADS-B squitters with CRC */
#define MADSB_MODE_TIMECODE      0x10
#define MADSB_MODE_FRAMENUMBER   0x20

extern int ma_setbaud(int fd);
extern int ma_init(const char *devname);
extern int ma_open(const char *devname);

#endif
