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
        Actuator actuators[2] = {
                {10.0, 5.0, 10.0, 3.0, 0.5, 0.0, 0.0, 0.0},
                {20.0, 7.0, 8.0, 3.5, 1.0, 0.0, 0.0, 0.0}
        };
        printf("===================== INITIAL STATE ======================\n");
        printf("v1 = output : %f, prev_error : %f, prev_I : %f\n, Kd : %f, Ki : %f, Kp : %f\n, currpoint : %f, setpoint : %f\n",actuators[0].output, actuators[0].prev_error,actuators[0].prev_I,actuators[0].Kd,actuators[0].Ki,actuators[0].Kp,actuators[0].currpoint,actuators[0].setpoint);
        printf("v2 = output : %f, prev_error : %f, prev_I : %f\n, Kd : %f, Ki : %f,Kp : %f\n, currpoint : %f, setpoint : %f\n",actuators[1].output,actuators[1].prev_error,actuators[0].prev_I,actuators[1].Kd,actuators[1].Ki,actuators[1].Kp,actuators[1].currpoint,actuators[1].setpoint);

        double dt = 0.001;
        __asm__ volatile (
                        "vsetivli t0 ,8, e32, m1,ta,ma\n\t"
                        "vle32.v v1, (%0) \n\t"
                        "addi t1, %0, 32 \n\t"
                        "vle32.v v2, (t1) \n\t"
                        "fld fa0, (%1) \n\t"
                        ".word 0b00000110000101010101000101010111\n\t"
                        "vse32.v v1, (%0) \n\t"
                        "vse32.v v2, (t1) \n\t"
                        :
                        : "r"(actuators), "r"(&dt) 
                        : "t0", "t1", "fa0", "v1", "v2", "memory"
                        );
        printf("\n=================== AFTER EXECUTION ====================\n");

        printf("v1 = output : %f, prev_error : %f, prev_I : %f\n, Kd : %f, Ki : %f, Kp : %f\n, currpoint : %f, setpoint : %f\n",actuators[0].output,actuators[0].prev_error,actuators[0].prev_I,actuators[0].Kd,actuators[0].Ki,actuators[0].Kp,actuators[0].currpoint,actuators[0].setpoint);

        printf("v2 = output : %f, prev_error : %f, prev_I : %f\n, Kd : %f, Ki : %f,Kp : %f\n, currpoint : %f, setpoint : %f\n",actuators[1].output,actuators[1].prev_error,actuators[1].prev_I,actuators[1].Kd,actuators[1].Ki,actuators[1].Kp,actuators[1].currpoint,actuators[1].setpoint);

        return 0;
}
