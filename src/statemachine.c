#include <stdint.h>
#include <unistd.h>                        //Needed for I2C port
#include <fcntl.h>                         //Needed for I2C port
#include <sys/ioctl.h>                     //Needed for I2C port
#include <linux/i2c-dev.h>                 //Needed for I2C port
#include <linux/i2c.h>                     //Needed for I2C port
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>

#include "common.h"
#include "statemachine.h"

const char *inputTokens[] = {
    "NO_ACTION",
    "V_TX",
    "U_TX",
    "L_TX",
    "PWR_ON",
    "OPERATE",
    "S_ON",
    "S_OFF",
    "KILL",

    "V_LEFT",
    "V_RIGHT",
    "V_TX_ON",
    "V_TX_OFF",
    "SHUTDOWN",

    "U_LEFT",
    "U_RIGHT",
    "U_TX_ON",
    "U_TX_OFF",

    "L_TX_ON",
    "L_TX_OFF",

    "EXIT",
    "STATUS",
    "MAX_TOKENS"
};

//I2C bus
static char *i2cdev = DEFAULT_I2C_DEV;
static int file_i2c;
static int i2caddr = DEFAULT_I2C_ADDR;                           //The I2C address of the slave
static uint8_t reg_gpioa_bits;
static uint8_t reg_gpiob_bits;


/*
   Overview
   ---------
   The code is checks for input token in a loop using getInput().The token is validated in processToken().
   If the token is good, approriate nextstate is stored in pwr_Config and changeState() is called.
*/

void *statemachine(void *argp){
    syslog (LOG_INFO,"Started State Machine");
    initialize();
    //define signals that will be handled.
    signal(SIGALRM, handle_alarm_signal);  //The 2 minute cooldown counter creates this signal.

    while(1){
        // initialize token to NO_ACTION
        pwrConfig.token = NO_ACTION;

        getInput();

        if(pwrConfig.token == NO_ACTION){
            continue;
        }

        if(pwrConfig.token == EXIT){
            break;
        }

        if(pwrConfig.token == STATUS){
            printf("Pin status: 0x%x 0x%x \n",reg_gpioa_bits,reg_gpiob_bits);
            printf("State: %d \n", pwrConfig.state);
            printf("Secondary state: %d \n", pwrConfig.sec_state);

            printf("Next State: %d \n", pwrConfig.next_state);
            printf("Next Secondary state: %d \n", pwrConfig.next_sec_state);
            continue;
        }

        processToken();

        if (pwrConfig.token != NO_ACTION)
            changeState();
            /*raise(SIGUSR1);*/
    }

    raise(SIGTERM);
    return 0;
}

void handle_alarm_signal(int sig){
    if (pwrConfig.state == V_TRAN && pwrConfig.sec_state == V_PA_COOL){
        pwrConfig.sec_state = V_PA_DOWN;
        MPC23017BitClear(V_PA);
        MPC23017BitClear(V_KEY);
        pwrConfig.state = BAND_SWITCH;
        pwrConfig.sec_state = NONE;
    }
    else if (pwrConfig.state == U_TRAN && pwrConfig.sec_state == U_PA_COOL){
        pwrConfig.sec_state = U_PA_DOWN;
        MPC23017BitClear(U_PA);
        MPC23017BitClear(U_KEY);
        pwrConfig.state = BAND_SWITCH;
        pwrConfig.sec_state = NONE;
    }
    else if (pwrConfig.state == L_TRAN && pwrConfig.sec_state == L_PA_COOL){
        pwrConfig.sec_state = L_PA_DOWN;
        MPC23017BitClear(L_PA);
        pwrConfig.state = BAND_SWITCH;
        pwrConfig.sec_state = NONE;
    }
    else
        stateWarning();
}


int initialize(void){

    uint8_t buffer[3] = {0};
    uint8_t length;

    pwrConfig.state = PWR_UP;
    pwrConfig.sec_state = NONE;

    if ((file_i2c = open(i2cdev, O_RDWR)) < 0)
    {
        syslog (LOG_ERR,"Failed to open the i2c bus");
        exit(EXIT_FAILURE);
    }
    else
    {
        syslog (LOG_INFO,"Successfully opened I2C bus \n");
    }

    //acquire i2c bus
    if (ioctl(file_i2c, I2C_SLAVE, i2caddr) < 0)
    {
        syslog (LOG_ERR,"Failed to acquire bus access and/or talk to slave.");
        exit(EXIT_FAILURE);
    }
    else
    {
        syslog (LOG_INFO,"Successfully acquired bus. \n");
    }


    //make GPIOA as output
    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg iomsg[2];
    uint8_t buf[2];
    int rc;

    buf[0] = 0x00;
    buf[1] = 0x00;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    if (rc < 0)
        syslog (LOG_ERR,"ioctl error return code : %d \n",rc);


    //make GPIOB as output
    buf[0] = 0x01;
    buf[1] = 0x00;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    if (rc < 0)
        syslog (LOG_ERR,"ioctl error return code : %d \n",rc);

    MPC23017BitReset();

}


//Handles proper exit after a crash or user EXIT token
int i2c_exit(void){
    int rc;

    MPC23017BitReset();

    rc=close(file_i2c);
    syslog (LOG_INFO,"closed i2c file with rc : %d \n",rc);

    return rc;
}


//Get user token and validate with list of input tokens.
int getInput(void){
    int i;

    sem_wait(&msgpending);
    for(i=0;i<MAX_TOKENS;i++){
        if(!strcmp(msg, inputTokens[i])){
            pwrConfig.token = i;
            syslog (LOG_INFO,"Token entered %s \n",inputTokens[i]);
            break;
        }
    }
    sem_post(&msgpending);
    sleep(1);

    if(i == MAX_TOKENS) {
        syslog (LOG_WARNING,"Not a known token. No action taken. \n");
    }

}


int processToken(void){

    if(pwrConfig.token == KILL){
        pwrConfig.next_state = PWR_UP;
        pwrConfig.next_sec_state =  NONE;
        return 0;
    }

    switch(pwrConfig.state){
        case SYS_KILL:
        case PWR_UP:
            if(pwrConfig.token == PWR_ON)
                pwrConfig.next_state = SYS_PWR_ON;
            else
                tokenError();
            break;
        case SYS_PWR_ON:
            if(pwrConfig.token == OPERATE)
                pwrConfig.next_state = BAND_SWITCH;
            else
                tokenError();
            break;
        case BAND_SWITCH:
            if(pwrConfig.token == S_ON)
                pwrConfig.next_state = S_SYS_ON;
            else if(pwrConfig.token == S_OFF)
                pwrConfig.next_state = S_SYS_OFF;
            else if(pwrConfig.token == V_TX){
                pwrConfig.next_state = V_TRAN;
                pwrConfig.next_sec_state = VHF_TRANSMIT;
            }
            else if(pwrConfig.token == U_TX){
                pwrConfig.next_state = U_TRAN;
                pwrConfig.next_sec_state = UHF_TRANSMIT;
            }
            else if(pwrConfig.token == L_TX){
                pwrConfig.next_state = L_TRAN;
                pwrConfig.next_sec_state = L_TRANSMIT;
            }
            else
                tokenError();
            break;
        case S_SYS_ON:
            BandSwitchErrorRecovery();
            break;

        case S_SYS_OFF:
            BandSwitchErrorRecovery();
            break;

        case V_TRAN:
            processVHFTokens();
            break;

        case U_TRAN:
            processUHFTokens();
            break;

        case L_TRAN:
            processLBandTokens();
            break;

        default:
            tokenError();
            break;
    }
}


int processVHFTokens(void){
    switch(pwrConfig.sec_state){
        case VHF_TRANSMIT:
        case V_SWITCH:
            if(pwrConfig.token == V_LEFT)
                pwrConfig.next_sec_state =  V_LHCP;
            else if(pwrConfig.token == V_RIGHT)
                pwrConfig.next_sec_state =  V_RHCP;
            else if(pwrConfig.token == V_TX_ON)
                pwrConfig.next_sec_state =  V_TRANS_ON;
            else if(pwrConfig.token == V_TX_OFF)
                pwrConfig.next_sec_state =  V_TRANS_OFF;
            else if(pwrConfig.token == U_RIGHT)
                pwrConfig.next_sec_state =  V_UHF_RHCP;
            else if(pwrConfig.token == U_LEFT)
                pwrConfig.next_sec_state =  V_UHF_LHCP;
            else if(pwrConfig.token == SHUTDOWN)
                pwrConfig.next_sec_state =  V_SHUTDOWN;
            else
                tokenError();
            break;
        case V_LHCP:
        case V_RHCP:
        case V_UHF_RHCP:
        case V_UHF_LHCP:
        case V_PA_DOWN:
        case V_TRANS_ON:
        case V_TRANS_OFF:
            VHFErrorRecovery();
            break;
        case V_SHUTDOWN:
        case V_PA_COOL:
            CoolDown_Wait();
            break;
        default:
            tokenError();
            break;
    }
}


int processUHFTokens(void){
    switch(pwrConfig.sec_state){
        case UHF_TRANSMIT:
        case U_SWITCH:
            if(pwrConfig.token == U_LEFT)
                pwrConfig.next_sec_state =  U_LHCP;
            else if(pwrConfig.token == U_RIGHT)
                pwrConfig.next_sec_state =  U_RHCP;
            else if(pwrConfig.token == U_TX_ON)
                pwrConfig.next_sec_state =  U_TRANS_ON;
            else if(pwrConfig.token == U_TX_OFF)
                pwrConfig.next_sec_state =  U_TRANS_OFF;
            else if(pwrConfig.token == V_RIGHT)
                pwrConfig.next_sec_state =  U_VHF_RHCP;
            else if(pwrConfig.token == V_LEFT)
                pwrConfig.next_sec_state =  U_VHF_LHCP;
            else if(pwrConfig.token == SHUTDOWN)
                pwrConfig.next_sec_state =  U_SHUTDOWN;
            else
                tokenError();
            break;
        case U_LHCP:
        case U_RHCP:
        case U_VHF_RHCP:
        case U_VHF_LHCP:
        case U_PA_DOWN:
        case U_TRANS_ON:
        case U_TRANS_OFF:
            UHFErrorRecovery();
            break;
        case U_SHUTDOWN:
        case U_PA_COOL:
            CoolDown_Wait();
            break;
        default:
            tokenError();
            break;
    }
}


int processLBandTokens(void){
    switch(pwrConfig.sec_state){
        case L_TRANSMIT:
        case L_SWITCH:
            if(pwrConfig.token == U_LEFT)
                pwrConfig.next_sec_state =  L_UHF_LHCP;
            else if(pwrConfig.token == U_RIGHT)
                pwrConfig.next_sec_state =  L_UHF_RHCP;
            else if(pwrConfig.token == L_TX_ON)
                pwrConfig.next_sec_state =  L_TRANS_ON;
            else if(pwrConfig.token == L_TX_OFF)
                pwrConfig.next_sec_state =  L_TRANS_OFF;
            else if(pwrConfig.token == V_RIGHT)
                pwrConfig.next_sec_state =  L_VHF_RHCP;
            else if(pwrConfig.token == V_LEFT)
                pwrConfig.next_sec_state =  L_VHF_LHCP;
            else if(pwrConfig.token == SHUTDOWN)
                pwrConfig.next_sec_state =  L_SHUTDOWN;
            else
                tokenError();
            break;
        case L_VHF_LHCP:
        case L_VHF_RHCP:
        case L_UHF_RHCP:
        case L_UHF_LHCP:
        case L_PA_DOWN:
        case L_TRANS_ON:
        case L_TRANS_OFF:
            LErrorRecovery();
            break;
        case L_SHUTDOWN:
        case L_PA_COOL:
            CoolDown_Wait();
            break;
        default:
            tokenError();
            break;
    }
}



int BandSwitchErrorRecovery(void){
    syslog(LOG_WARNING,"The system should not have been in this state. Corrective action taken.");
    syslog(LOG_WARNING,"Please reenter your token and manually validate the action.");
    pwrConfig.next_state = BAND_SWITCH;
}


int tokenError(void){
    syslog (LOG_WARNING,"Token not valid for the state. Please refer to state diagram. No action taken.");
    pwrConfig.token = NO_ACTION;
}

int VHFErrorRecovery(void){
    syslog(LOG_WARNING,"The system should not have been in this state. Corrective action taken \n");
    syslog(LOG_WARNING,"Please reenter your token and manually validate the action. \n");
    pwrConfig.next_state = V_SWITCH;
}

int UHFErrorRecovery(void){
    syslog(LOG_WARNING,"The system should not have been in this state. Corrective action taken \n");
    syslog(LOG_WARNING,"Please reenter your token and manually validate the action. \n");
    pwrConfig.next_state = U_SWITCH;
}

int LErrorRecovery(void){
    syslog(LOG_WARNING,"The system should not have been in this state. Corrective action taken \n");
    syslog(LOG_WARNING,"Please reenter your token and manually validate the action. \n");
    pwrConfig.next_state = L_SWITCH;
}

void stateError(void){
    syslog(LOG_ERR,"ERROR: There is a program error. Contact coder. \n");
    syslog(LOG_ERR,"Results unpredictable. Please Kill and start over. \n");
}

void stateWarning(void){
    syslog(LOG_WARNING,"The system should not have been in this state. KILL token likely entered before.");
}


int CoolDown_Wait(void){
    syslog(LOG_WARNING,"Waiting for cooldown.No action taken.If required, force exit via KILL or EXIT tokens. \n");
    pwrConfig.token = NO_ACTION;
}


int changeState(void){

    uint8_t temporary;

    switch(pwrConfig.next_state){
        case SYS_KILL:
        case PWR_UP:
            MPC23017BitReset();
            pwrConfig.state = PWR_UP;
            pwrConfig.sec_state = NONE;
            break;
        case SYS_PWR_ON:
            MPC23017BitSet(SDR_ROCK);
            MPC23017BitSet(SDR_LIME);
            MPC23017BitSet(ROT_PWR);
            pwrConfig.state = SYS_PWR_ON;
            break;
        case BAND_SWITCH:
            pwrConfig.state = BAND_SWITCH;
            break;
        case S_SYS_ON:
            MPC23017BitSet(S_PWR);
            pwrConfig.state = BAND_SWITCH;
            break;
        case S_SYS_OFF:
            MPC23017BitClear(S_PWR);
            pwrConfig.state = BAND_SWITCH;
            break;

        case V_TRAN:
            pwrConfig.state = V_TRAN;
            switch(pwrConfig.next_sec_state){
                case VHF_TRANSMIT:
                    MPC23017BitSet(U_LNA);
                    MPC23017BitSet(V_PA);
                    MPC23017BitSet(V_KEY);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                case V_SWITCH:
                    break;
                case V_SHUTDOWN:
                    MPC23017BitClear(U_LNA);
                    MPC23017BitClear(U_POL);
                    MPC23017BitClear(V_POL);
                    MPC23017BitClear(V_PTT);
                    pwrConfig.sec_state = V_SHUTDOWN;

                    pwrConfig.sec_state = V_PA_COOL;
                    alarm(120);
                    break;
                case V_PA_COOL:
                case V_PA_DOWN:
                    break;
                case V_UHF_LHCP:
                    MPC23017BitSet(U_POL);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                case V_UHF_RHCP:
                    MPC23017BitClear(U_POL);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                case V_TRANS_ON:
                    MPC23017BitSet(V_PTT);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                case V_TRANS_OFF:
                    MPC23017BitClear(V_PTT);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                case V_LHCP:
                    temporary = MPC23017BitRead(V_PTT);
                    MPC23017BitClear(V_PTT);
                    usleep(100);
                    MPC23017BitSet(V_POL);
                    usleep(100);
                    if(temporary)
                        MPC23017BitSet(V_PTT);
                    else
                        MPC23017BitClear(V_PTT);
                    pwrConfig.sec_state = V_SWITCH;
                    break;

                case V_RHCP:
                    temporary = MPC23017BitRead(V_PTT);
                    MPC23017BitClear(V_PTT);
                    usleep(100);
                    MPC23017BitClear(V_POL);
                    usleep(100);
                    if(temporary)
                        MPC23017BitSet(V_PTT);
                    else
                        MPC23017BitClear(V_PTT);
                    pwrConfig.sec_state = V_SWITCH;
                    break;
                default:
                    stateError();
                    break;
            }
            break;

        case U_TRAN:
            pwrConfig.state = U_TRAN;
            switch(pwrConfig.next_sec_state){
                case UHF_TRANSMIT:
                    MPC23017BitSet(V_LNA);
                    MPC23017BitSet(U_PA);
                    MPC23017BitSet(U_KEY);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                case U_SWITCH:
                    break;
                case U_SHUTDOWN:
                    MPC23017BitClear(V_LNA);
                    MPC23017BitClear(V_POL);
                    MPC23017BitClear(U_POL);
                    MPC23017BitClear(U_PTT);
                    pwrConfig.sec_state = U_SHUTDOWN;

                    pwrConfig.sec_state = U_PA_COOL;
                    alarm(120);
                    break;
                case U_PA_COOL:
                case U_PA_DOWN:
                    break;
                case U_VHF_LHCP:
                    MPC23017BitSet(V_POL);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                case U_VHF_RHCP:
                    MPC23017BitClear(V_POL);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                case U_TRANS_ON:
                    MPC23017BitSet(U_PTT);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                case U_TRANS_OFF:
                    MPC23017BitClear(U_PTT);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                case U_LHCP:
                    temporary = MPC23017BitRead(U_PTT);
                    MPC23017BitClear(U_PTT);
                    usleep(100);
                    MPC23017BitSet(U_POL);
                    usleep(100);
                    if(temporary)
                        MPC23017BitSet(U_PTT);
                    else
                        MPC23017BitClear(U_PTT);
                    pwrConfig.sec_state = U_SWITCH;
                    break;

                case U_RHCP:
                    temporary = MPC23017BitRead(U_PTT);
                    MPC23017BitClear(U_PTT);
                    usleep(100);
                    MPC23017BitClear(U_POL);
                    usleep(100);
                    if(temporary)
                        MPC23017BitSet(U_PTT);
                    else
                        MPC23017BitClear(U_PTT);
                    pwrConfig.sec_state = U_SWITCH;
                    break;
                default:
                    stateError();
                    break;
            }
            break;

        case L_TRAN:
            pwrConfig.state = L_TRAN;
            switch(pwrConfig.next_sec_state){
                case L_TRANSMIT:
                    MPC23017BitSet(U_LNA);
                    MPC23017BitSet(V_LNA);
                    MPC23017BitSet(L_PA);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_SWITCH:
                    break;
                case L_SHUTDOWN:
                    MPC23017BitClear(L_PTT);
                    MPC23017BitClear(U_POL);
                    MPC23017BitClear(V_POL);
                    MPC23017BitClear(V_LNA);
                    MPC23017BitClear(U_LNA);
                    pwrConfig.sec_state = L_SHUTDOWN;

                    pwrConfig.sec_state = L_PA_COOL;
                    alarm(120);
                    break;
                case L_PA_COOL:
                case L_PA_DOWN:
                    break;
                case L_UHF_LHCP:
                    MPC23017BitSet(U_POL);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_UHF_RHCP:
                    MPC23017BitClear(U_POL);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_TRANS_ON:
                    MPC23017BitSet(L_PTT);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_TRANS_OFF:
                    MPC23017BitClear(L_PTT);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_VHF_LHCP:
                    MPC23017BitSet(V_POL);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                case L_VHF_RHCP:
                    MPC23017BitClear(V_POL);
                    pwrConfig.sec_state = L_SWITCH;
                    break;
                default:
                    stateError();
                    break;
            }
            break;

        default:
            stateError();
            break;
    }
}


//All GPIO bit values are stored in program variable.
//Set a bit value using ioctl in one of GPIOs and update the program variable.
int MPC23017BitSet(int bit){

    uint8_t buffer[3] = {0};
    uint8_t shift_value = 0;
    uint8_t reg_address = 0;
    uint8_t reg_value = 0;
    uint8_t length;

    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg iomsg[2];
    uint8_t buf[2];
    int rc;

    if (bit<8){
        reg_address = 0x12;
        shift_value = bit;
        reg_gpioa_bits = reg_gpioa_bits | (1 << shift_value);
        reg_value = reg_gpioa_bits;
    }
    else{
        reg_address = 0x13;
        shift_value = bit - 8;
        reg_gpiob_bits = reg_gpiob_bits | (1 << shift_value);
        reg_value = reg_gpiob_bits;
    }

    //update register value
    buf[0] = reg_address;
    buf[1] = reg_value;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    if (rc < 0)
        syslog(LOG_ERR,"ioctl bit set error: %s",strerror(errno));

}


int MPC23017BitClear(int bit){
    uint8_t buffer[3] = {0};
    uint8_t shift_value = 0;
    uint8_t reg_address = 0;
    uint8_t reg_value = 0;
    uint8_t mask = 0xFF;
    uint8_t length;

    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg iomsg[2];
    uint8_t buf[2];
    int rc;

    if (bit<8){
        reg_address = 0x12;
        shift_value = bit;
        mask = 0xFF ^ (1 << shift_value);
        reg_gpioa_bits = reg_gpioa_bits & mask;
        reg_value   = reg_gpioa_bits;
    }
    else{
        reg_address = 0x13;
        shift_value = bit - 8;
        mask = 0xFF ^ (1 << shift_value);
        reg_gpiob_bits = reg_gpiob_bits & mask;
        reg_value   = reg_gpiob_bits;
    }


    //new register value
    mask = 0xFF ^ (1 << shift_value);
    reg_value = reg_value & mask;

    //write the new value
    buf[0] = reg_address;
    buf[1] = reg_value;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    if (rc < 0)
        syslog(LOG_ERR,"ioctl bit clear error: %s",strerror(errno));
}


int MPC23017BitReset(void){
    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg iomsg[2];
    uint8_t buf[2];
    int rc;

    //reset GPIOA
    buf[0] = 0x12;
    buf[1] = 0x00;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    reg_gpioa_bits = 0x00;
    syslog(LOG_INFO,"GPIOA reset %d \n",rc);
    if (rc < 0)
        syslog(LOG_ERR,"ioctl gpioa reset error: %s",strerror(errno));

    //reset GPIOB
    buf[0] = 0x13;
    buf[1] = 0x00;

    iomsg[0].addr = i2caddr;
    iomsg[0].flags = 0;
    iomsg[0].buf = buf;
    iomsg[0].len = 2;

    msgset.msgs = iomsg;
    msgset.nmsgs = 1;

    rc = ioctl(file_i2c,I2C_RDWR,&msgset);
    syslog(LOG_INFO,"GPIOB reset %d \n",rc);
    if (rc < 0)
        syslog(LOG_ERR,"ioctl gpiob reset error: %s",strerror(errno));

    reg_gpioa_bits = 0x00;
    reg_gpiob_bits = 0x00;

}

//This does not read the actual value. It reads the data from program variables
int MPC23017BitRead(int bit){
    uint8_t shift_value = 0;
    uint8_t gpio_bits=0;

    if (bit<8){
        shift_value = bit;
        gpio_bits = reg_gpioa_bits & (1 << shift_value);
    }
    else{
        shift_value = bit - 8;
        gpio_bits = reg_gpiob_bits & (1 << shift_value);
    }

    if (gpio_bits == 0 )
        return 0;
    else
        return 1;

}

