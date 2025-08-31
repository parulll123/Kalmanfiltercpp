#include <SimpleModbusSlave.h>
enum 
{
  DATA1,
  DATA2,
  DATA3,
  DATA4,
  DATA5,
  DATA6,
  DATA7,
  DATA8,
  DATA9,
  DATA10,
  DATA11,
  DATA12,
  DATA13,
  DATA14,
  DATA15,
  DATA16,
  DATA17,
  DATA18,
  DATA19,
  DATA20,
  DATA21,
  DATA22,
  DATA23,
  DATA24,
  DATA25,
  DATA26,
  TOTAL_ERRORS,
  TOTAL_REGS_SIZE
};
unsigned int holdingRegs[TOTAL_REGS_SIZE]; 
void init_modbus(){
  modbus_configure(9600, 200, 13, TOTAL_REGS_SIZE, 0);
}
