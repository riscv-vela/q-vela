#include <stdio.h>
#include <stdint.h>

typedef struct {
        float setpoint;
        float currpoint;
        float Kp,Ki,Kd;
        float prev_I;
        float prev_error;
        float output;
} Actuator;

int main() {
        Actuator actuators[9] = {
                {10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0},
                {20.0, 7.0, 8.0, 3.5, 1.0, 0.0, 0.0, 0.0},
                {10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0},
                {20.0, 7.0, 8.0, 3.5, 1.0, 0.0, 0.0, 0.0},
                {10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0},
                {20.0, 7.0, 8.0, 3.5, 1.0, 0.0, 0.0, 0.0},
                {10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0},
                {20.0, 7.0, 8.0, 3.5, 1.0, 0.0, 0.0, 0.0},
		{10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0}
    	};
        printf("===================== INITIAL STATE ======================\n");
        printf("v1 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f, Kp : %f, currpoint : %f, setpoint : %f\n",actuators[0].output, actuators[0].prev_error,actuators[0].prev_I,actuators[0].Kd,actuators[0].Ki,actuators[0].Kp,actuators[0].currpoint,actuators[0].setpoint);
        printf("v2 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[1].output,actuators[1].prev_error,actuators[0].prev_I,actuators[1].Kd,actuators[1].Ki,actuators[1].Kp,actuators[1].currpoint,actuators[1].setpoint);
	printf("v3 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[2].output,actuators[2].prev_error,actuators[2].prev_I,actuators[2].Kd,actuators[2].Ki,actuators[2].Kp,actuators[2].currpoint,actuators[2].setpoint);
	printf("v4 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[3].output,actuators[3].prev_error,actuators[3].prev_I,actuators[3].Kd,actuators[3].Ki,actuators[3].Kp,actuators[3].currpoint,actuators[3].setpoint);
	printf("v5 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[4].output,actuators[4].prev_error,actuators[4].prev_I,actuators[4].Kd,actuators[4].Ki,actuators[4].Kp,actuators[4].currpoint,actuators[4].setpoint);
	printf("v6 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[5].output,actuators[5].prev_error,actuators[5].prev_I,actuators[5].Kd,actuators[5].Ki,actuators[5].Kp,actuators[5].currpoint,actuators[5].setpoint);

        double dt = 0.001;
        __asm__ volatile (
                        "vsetivli t0 ,8, e32, m1,ta,ma\n\t"
                        "vle32.v v1, (%0) \n\t"
                        "addi t1, %0, 32 \n\t"
                        "vle32.v v2, (t1) \n\t"
                        "addi t2, %0, 64 \n\t"
                        "vle32.v v3, (t2) \n\t"
                        "addi t3, %0, 96 \n\t"
                        "vle32.v v4, (t3) \n\t"
                        "addi t4, %0, 128 \n\t"
                        "vle32.v v5, (t4) \n\t"
                        "addi t5, %0, 160 \n\t"
                        "vle32.v v6, (t5) \n\t"
			"addi a0, %0, 192\n\t"
    			"vle32.v v7, (a0)\n\t"                
			"addi a1, %0, 224\n\t"
 			"vle32.v v8, (a1)\n\t"                 
			"addi a2, %0, 256\n\t"
			"vle32.v v31, (a2)\n\t"

                        "fld fa0, (%1) \n\t"
                        ".word 0x06155FD7\n\t"
                        "vse32.v v1, (%0) \n\t"
                        "vse32.v v2, (t1) \n\t"
                        "vse32.v v3, (t2) \n\t"
                        "vse32.v v4, (t3) \n\t"
                        "vse32.v v5, (t4) \n\t"
                        "vse32.v v6, (t5) \n\t"
                        "vse32.v v7, (a0) \n\t"
                        "vse32.v v8, (a1) \n\t"
			"vse32.v v31, (a2) \n\t"
			:
                        : "r"(actuators), "r"(&dt) 
                        : "a0", "a1","a2","t0", "t1", "fa0", "v1", "v2", "memory","t2","t3","t4","t5"
                        );
        printf("\n=================== AFTER EXECUTION ====================\n");

        printf("v1 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f, Kp : %f, currpoint : %f, setpoint : %f\n",actuators[0].output,actuators[0].prev_error,actuators[0].prev_I,actuators[0].Kd,actuators[0].Ki,actuators[0].Kp,actuators[0].currpoint,actuators[0].setpoint);
        printf("v2 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[1].output,actuators[1].prev_error,actuators[1].prev_I,actuators[1].Kd,actuators[1].Ki,actuators[1].Kp,actuators[1].currpoint,actuators[1].setpoint);
	printf("v3 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[2].output,actuators[2].prev_error,actuators[2].prev_I,actuators[2].Kd,actuators[2].Ki,actuators[2].Kp,actuators[2].currpoint,actuators[2].setpoint);
	printf("v4 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[3].output,actuators[3].prev_error,actuators[3].prev_I,actuators[3].Kd,actuators[3].Ki,actuators[3].Kp,actuators[3].currpoint,actuators[3].setpoint);
	printf("v5 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[4].output,actuators[4].prev_error,actuators[4].prev_I,actuators[4].Kd,actuators[4].Ki,actuators[4].Kp,actuators[4].currpoint,actuators[4].setpoint);
	printf("v6 = output : %f, prev_error : %f, prev_I : %f, Kd : %f, Ki : %f,Kp : %f, currpoint : %f, setpoint : %f\n",actuators[5].output,actuators[5].prev_error,actuators[5].prev_I,actuators[5].Kd,actuators[5].Ki,actuators[5].Kp,actuators[5].currpoint,actuators[5].setpoint);
	printf("v7 = output : %f, prev_error : %f\n",actuators[6].output,actuators[6].prev_error);
	printf("v8 = output : %f, prev_error : %f\n",actuators[7].output,actuators[7].prev_error);
	printf("v32 = output : %f, prev_error : %f\n",actuators[8].output,actuators[8].prev_error);
        return 0;
}
