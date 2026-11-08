// Host stand-in for the IMX SpiBus driver header that FT812.h includes. The host build talks to an
// EveDevice instead of a SPI peripheral; only the names FT812.h mentions are needed.
#ifndef HOST_STUB_SPIBUS_H_
#define HOST_STUB_SPIBUS_H_

#include <stdint.h>

struct GPIO_Type;
class SpiBus
{
};

#endif
