#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <math.h>
#include "mcp9808.h"

void MCP9808SetSlave(int i2c_fd)
{
	/* Set MCP9808 as slave device */
	if (ioctl(i2c_fd, I2C_SLAVE, MCP9808_I2C_ADDR) < 0) {
		logmsg(LOG_ERR, "Error: Failed setting MCP9808 as slave: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

float MCP9808GetTemp(int i2c_fd)
{
	int regval;

	MCP9808SetSlave(i2c_fd);

	if ((regval = i2c_smbus_read_word_data(i2c_fd, MCP9808_TEMP_REG)) < 0) {
		logmsg(LOG_ERR, "Error: Failed reading current GPIO output state: %s\n", strerror(errno));
		return NAN;
	}

	if (regval & TEMP_SIGN_MASK)
		return (256 - (((regval & TEMP_UVAL_MASK) * 16.0) + (((regval & TEMP_LVAL_MASK) >> 8) / 16.0)));
	else
		return (((regval & TEMP_UVAL_MASK) * 16.0) + (((regval & TEMP_LVAL_MASK) >> 8) / 16.0));
}
