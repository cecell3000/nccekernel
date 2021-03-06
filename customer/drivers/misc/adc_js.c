/*
 * drivers/amlogic/input/adc_keypad/adc_keypad.c
 *
 * ADC Keypad Driver
 *
 * Copyright (C) 2010 Amlogic, Inc.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * author :   Robin Zhu
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/saradc.h>
#include <linux/adc_keypad.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>

#define BUTTON_A        BTN_A
#define BUTTON_B        BTN_B
#define BUTTON_X        BTN_X
#define BUTTON_Y        BTN_Y
#define BUTTON_L        BTN_TL
#define BUTTON_R        BTN_TR
#define BUTTON_L2       BTN_TL2
#define BUTTON_R2       BTN_TR2

#define LCD_SCREEN_X    1024
#define LCD_SCREEN_Y    600
#define TRACKING_ID 10
#define ADC_KEY     200
#define ADC_VALUE   60//100
#define XCENTER     100
#define SPEED       5

#define CENTER_X 512
#define CENTER_Y 300

#define STEP1       (512 - ADC_VALUE)
#define STEP2       (512 - ADC_VALUE*2)
#define STEP3       (512 - ADC_VALUE*3)
#define STEP4       (512 - ADC_VALUE*4)
#define STEP5       (512 - ADC_VALUE*5)
#define STEP6       (512 - ADC_VALUE*6)
#define STEP7       (512 - ADC_VALUE*7 + 10)
#define STEP8       (512 - ADC_VALUE*7 - 20)
#define VALUE1      1
#define VALUE2      1
#define VALUE3      2
#define VALUE4      2
#define VALUE5      3
#define VALUE6      4
#define VALUE7      6
#define VALUE8      9

#define X1      1
#define X2      2
#define X3      3
#define X4      4
#define X5      5
#define VX1     6
#define VX2     4
#define VX3     3
#define VX4     2
#define VX5     1

#define CENTER_TRY  3

//1, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view,view_x, view_y
//2, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y, view_x, view_y, view_r
static long key_param[109];

struct kp {
    struct input_dev *input;
    struct timer_list timer;
    unsigned int cur_keycode[SARADC_CHAN_NUM];
    unsigned int cur_keycode_status[SARADC_CHAN_NUM];
    unsigned int tmp_code[SARADC_CHAN_NUM];
    int count[SARADC_CHAN_NUM];
    int config_major;
    char config_name[20];
    struct class *config_class;
    struct device *config_dev;
    int chan[SARADC_CHAN_NUM];
    int key_code[SARADC_CHAN_NUM];
    int key_value[SARADC_CHAN_NUM];
    int key_valid[SARADC_CHAN_NUM];
    int circle_flag[2];
    int old_x, old_y;
    int chan_num;
    struct adc_key *key;
    int key_num;
    struct work_struct work_update;
    int flaga, flagb, flagx, flagy, flagl, flagr, flagl2, flagr2;
    //Detect channel 4 (vol, start,select)
    int flagchan4;
};

static struct kp *gp_kp = NULL;

static int release = 1;
static int second0 = 0, second1 = 0;
static int swipe = 0;
static void key_report(struct kp *kp, long x, long y, int id) {
    if (x == 0 && y == 0) {
        //printk("---------- zero point --------------\n");
        ;
    } else {
        input_report_key(kp->input, BTN_TOUCH, 1);
        input_report_abs(kp->input, ABS_MT_TRACKING_ID, id);
        input_report_abs(kp->input, ABS_MT_TOUCH_MAJOR, 20);
        input_report_abs(kp->input, ABS_MT_WIDTH_MAJOR, 20);
        input_report_abs(kp->input, ABS_MT_POSITION_X, x);
        input_report_abs(kp->input, ABS_MT_POSITION_Y, y);
        input_mt_sync(kp->input);
    }
    release = 1;
}

static void key_swipe(struct kp *kp, long x, long y, int id) {
    if (x == 0 && y == 0) {
        //printk("---------- zero point --------------\n");
        ;
    } else {
        input_report_key(kp->input, BTN_TOUCH, 1);
        input_report_abs(kp->input, ABS_MT_TRACKING_ID, id);
        input_report_abs(kp->input, ABS_MT_TOUCH_MAJOR, 20);
        input_report_abs(kp->input, ABS_MT_WIDTH_MAJOR, 20);
        input_report_abs(kp->input, ABS_MT_POSITION_X, x);
        input_report_abs(kp->input, ABS_MT_POSITION_Y, y);
        input_mt_sync(kp->input);
    }
    release = 0;
}


static void kp_search_key(struct kp *kp) {
    int value, i;

    if (key_param[0] == 1 || key_param[0] == 2 || key_param[0] == 3) {
        //virtual key mode channel 0, 1, 2, 3
        for (i = 0; i < 4; i++) {

            value = get_adc_sample(kp->chan[i]);
            if (value < 0) {
                ;
            } else {
                if ((value >= 1023 / 2 - ADC_VALUE * 2)
                        && (value <= 1023 / 2 + ADC_VALUE * 2))
                    kp->key_valid[i] = 0;
                else
                    kp->key_valid[i] = 1;
                kp->key_value[i] = value;
            }
        }

        //VEKTOR: I add this to set valid inputs from channel 4
        value = get_adc_sample(kp->chan[4]);
        if (value < 0) {
            ;
        } else {
            if ((value >= 0 && value <= (9 + 40))
                    || (value >= (392 - 40) && value <= (392 + 40))
                    || (value >= (150 - 40) && value <= (150 + 40))
                    || (value >= (275 - 40) && value <= (275 + 40)))
                kp->key_valid[4] = 1;
            else
                kp->key_valid[4] = 0;
            kp->key_value[4] = value;
        }
    }
    if (key_param[0] == 0) {

        //normal key mode

        //VEKTOR_NOTE: Here we are in normal key mode when touchpad is disabled
        //to enable RSTICK hardware mapping we need to enable it in mode 0
        //Enabling channels 0,1
        //channel 0
        value = get_adc_sample(kp->chan[0]);
        if (value < 0) {
            ;
        } else {
            if (value >= 1023 - ADC_KEY)
                //rstick down
                kp->key_code[0] = KEY_K;
            else if (value <= 0 + ADC_KEY)
                //rstick up
                kp->key_code[0] = KEY_I;
            else
                kp->key_code[0] = 0;
        }

        //channel 1
        value = get_adc_sample(kp->chan[1]);
        if (value < 0) {
            ;
        } else {
            if (value >= 1023 - ADC_KEY)
                kp->key_code[1] = KEY_J; //rstick left
            else if (value <= 0 + ADC_KEY)
                kp->key_code[1] = KEY_L; //rstick right
            else
                kp->key_code[1] = 0;
        }

        //channel 2

        value = get_adc_sample(kp->chan[2]);
        if (value < 0) {
            ;
        } else {
            if (value >= 1023 - ADC_KEY)
                kp->key_code[2] = KEY_LEFT;
            else if (value <= 0 + ADC_KEY)
                kp->key_code[2] = KEY_RIGHT;
            else
                kp->key_code[2] = 0;
        }

        //channel 3
        value = get_adc_sample(kp->chan[3]);
        if (value < 0) {
            ;
        } else {
            if (value >= 1023 - ADC_KEY)
                kp->key_code[3] = KEY_UP;
            else if (value <= 0 + ADC_KEY)
                kp->key_code[3] = KEY_DOWN;
            else
                kp->key_code[3] = 0;
        }

    }

    //channel 4
    //VEKTOR Note: We add this inside here because we want channel 4 (volume,start,select)
    //to work only when we're not using enhanced virtual mapping
    if (key_param[0] != 3) {
        value = get_adc_sample(kp->chan[4]);
        if (value < 0) {
            ;
        } else {
            if (value >= 0 && value <= (9 + 40))
                kp->key_code[4] = KEY_SPACE;
            else if (value >= (392 - 40) && value <= (392 + 40))
                kp->key_code[4] = KEY_ENTER;
            else if (value >= (150 - 40) && value <= (150 + 40))
                kp->key_code[4] = KEY_VOLUMEDOWN;
            else if (value >= (275 - 40) && value <= (275 + 40))
                kp->key_code[4] = KEY_VOLUMEUP;
            else
                kp->key_code[4] = 0;
        }
    }

    return 0;
}


static void do_swipe(struct kp *kp, long xstart, long ystart, long xend, long yend,
        int id) {
    printk("SWIPE! X1 %d Y1 %d X2 %d Y2 %d ID %d \n",xstart,ystart,xend,yend,id);
    int sx, sy;
    int dx = abs(xend - xstart);
    int dy = abs(yend - ystart);
    if (xstart < xend)
        sx = 1;
    else
        sx = -1;
    if (ystart < yend)
        sy = 1;
    else
        sy = -1;
    int err = dx - dy;
    long tempx = xstart;
    long tempy = ystart;
    while (1) {
        key_report(kp, tempx, tempy, id);
        input_sync(kp->input);
        if(tempx == xend && tempy == yend) break;
        int e2 = 2 * err;
        if (e2 > (-1)*dy) {
            err = err - dy;
            tempx = tempx + sx;
        } else if (e2 < dx) {
            err = err + dx;
            tempy = tempy + sy;
        }
    }
    //release = 1;
    printk("END! X %d Y %d \n",tempx, tempy);

}


static void gpio_keys_init(void) {
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 3, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 4, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 5, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 6, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 7, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 8, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 9, 1);
    WRITE_CBUS_REG_BITS(PAD_PULL_UP_REG0, 1, 10, 1);

    WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_0, 0, 6, 1);
    WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_3, 0, 0, 3);
    WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_3, 0, 5, 1);
    WRITE_CBUS_REG_BITS(PERIPHS_PIN_MUX_6, 0, 19, 5);

    set_gpio_mode(GPIOA_bank_bit0_27(3), GPIOA_bit_bit0_27(3), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(4), GPIOA_bit_bit0_27(4), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(5), GPIOA_bit_bit0_27(5), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(6), GPIOA_bit_bit0_27(6), GPIO_INPUT_MODE);

    set_gpio_mode(GPIOA_bank_bit0_27(7), GPIOA_bit_bit0_27(7), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(8), GPIOA_bit_bit0_27(8), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(9), GPIOA_bit_bit0_27(9), GPIO_INPUT_MODE);
    set_gpio_mode(GPIOA_bank_bit0_27(10), GPIOA_bit_bit0_27(10),
            GPIO_INPUT_MODE);
}

//enable, circle_x, circle_y, r, ax, ay, bx, by, xx, xy, yx, yy, lx, ly, rx, ry, l2x, l2y, r2x, r2y
static int keya, keyb, keyx, keyy, keyl, keyr, keyl2, keyr2;
static int keya_old, keyb_old, keyx_old, keyy_old, keyl_old, keyr_old,
        keyl2_old, keyr2_old;
static void scan_keys(struct kp *kp) {
    struct input_dev *input = kp->input;

    keyl2 = get_gpio_val(GPIOA_bank_bit0_27(3), GPIOA_bit_bit0_27(3));
    keyl = get_gpio_val(GPIOA_bank_bit0_27(4), GPIOA_bit_bit0_27(4));
    keyr2 = get_gpio_val(GPIOA_bank_bit0_27(5), GPIOA_bit_bit0_27(5));
    keyr = get_gpio_val(GPIOA_bank_bit0_27(6), GPIOA_bit_bit0_27(6));
    keyx = get_gpio_val(GPIOA_bank_bit0_27(7), GPIOA_bit_bit0_27(7));
    keya = get_gpio_val(GPIOA_bank_bit0_27(8), GPIOA_bit_bit0_27(8));
    keyb = get_gpio_val(GPIOA_bank_bit0_27(9), GPIOA_bit_bit0_27(9));
    keyy = get_gpio_val(GPIOA_bank_bit0_27(10), GPIOA_bit_bit0_27(10));

    if (keyl == keyl_old) {
        if (key_param[0]) {
            if (keyl) {
                if (key_param[87] == 1 ) {
                    if(kp->flagl == 1)
                    do_swipe(kp, key_param[12], key_param[13], key_param[85],
                            key_param[86], 6);
                } else
                    key_report(kp, key_param[12], key_param[13], 6);
                input_report_key(input, BUTTON_L, 1);
                kp->flagl = 0;
            } else {
                input_report_key(input, BUTTON_L, 0);
                kp->flagl = 1;
            }
        } else if (keyl == kp->flagl) {
            if (keyl) {
                input_report_key(input, BUTTON_L, 1);
                input_mt_sync(input);
                kp->flagl = 0;
                printk("KEY L\n");
            } else {
                input_report_key(input, BUTTON_L, 0);
                input_mt_sync(input);
                kp->flagl = 1;
                printk("KEY L release\n");
            }
        }
    }

    if (keyr == keyr_old) {
        if (key_param[0]) {
            if (keyr) {

                if (key_param[90] == 1  ) {
                    if(kp->flagr==1)
                    do_swipe(kp, key_param[14], key_param[15], key_param[88],
                            key_param[89], 7);
                } else
                    key_report(kp, key_param[14], key_param[15], 7);
                kp->flagr = 0;
            } else {
                kp->flagr = 1;
            }
        } else if (keyr == kp->flagr) {
            if (keyr) {
                input_report_key(input, BUTTON_R, 1);
                input_mt_sync(input);
                kp->flagr = 0;
                printk("KEY R\n");
            } else {
                input_report_key(input, BUTTON_R, 0);
                input_mt_sync(input);
                kp->flagr = 1;
                printk("KEY R release\n");
            }
        }
    }

    if (keyl2 == keyl2_old) {
        if (key_param[0]) {
            if (keyl2) {
                if (key_param[93] == 1 ) {
                    if(kp->flagl2 == 1)
                    do_swipe(kp, key_param[16], key_param[17], key_param[91],
                            key_param[92], 8);
                } else
                    key_report(kp, key_param[16], key_param[17], 8);
                kp->flagl2 = 0;
            } else {
                kp->flagl2 = 1;
            }
        } else if (keyl2 == kp->flagl2) {
            if (keyl2) {
                input_report_key(input, BUTTON_L2, 1);
                input_mt_sync(input);
                kp->flagl2 = 0;
                printk("KEY L2\n");
            } else {
                input_report_key(input, BUTTON_L2, 0);
                input_mt_sync(input);
                kp->flagl2 = 1;
            }
        }
    }

    if (keyr2 == keyr2_old) {
        if (key_param[0]) {
            if (keyr2) {
                if (key_param[96] == 1 ) {
                    if(kp->flagr2 == 1)
                    do_swipe(kp, key_param[18], key_param[19], key_param[94],
                            key_param[95], 9);
                } else
                    key_report(kp, key_param[18], key_param[19], 9);
                kp->flagr2 = 0;
            } else {
                kp->flagr2 = 1;
            }
        } else if (keyr2 == kp->flagr2) {
            if (keyr2) {
                input_report_key(input, BUTTON_R2, 1);
                input_mt_sync(input);
                kp->flagr2 = 0;
                printk("KEY R2\n");
            } else {
                input_report_key(input, BUTTON_R2, 0);
                input_mt_sync(input);
                kp->flagr2 = 1;
            }
        }
    }

    if (keya == keya_old) {
        if (key_param[0]) {
            if (keya) {
                if (key_param[75] == 1 ) {
                    if(kp->flaga == 1)
                    do_swipe(kp, key_param[4], key_param[5], key_param[73],
                            key_param[74], 2);
                } else
                    key_report(kp, key_param[4], key_param[5], 2);
                kp->flaga = 0;
            } else {
                kp->flaga = 1;
            }
        } else if (keya == kp->flaga) {
            if (keya) {
                input_report_key(input, BUTTON_A, 1);
                input_mt_sync(input);
                kp->flaga = 0;
                printk("KEY A\n");
            } else {
                input_report_key(input, BUTTON_A, 0);
                input_mt_sync(input);
                kp->flaga = 1;
                printk("KEY A release\n");
            }
        }
    }

    if (keyb == keyb_old) {
        if (key_param[0]) {
            if (keyb) {
                if (key_param[78] == 1 ) {
                    if(kp->flagb == 1)
                    do_swipe(kp, key_param[6], key_param[7], key_param[76],
                            key_param[77], 3);

                } else
                    key_report(kp, key_param[6], key_param[7], 3);
                kp->flagb = 0;
            } else {
                kp->flagb = 1;
            }
        } else if (keyb == kp->flagb) {
            if (keyb) {
                input_report_key(input, BUTTON_B, 1);
                input_mt_sync(input);
                kp->flagb = 0;
                printk("KEY B\n");
            } else {
                input_report_key(input, BUTTON_B, 0);
                input_mt_sync(input);
                printk("KEY B release\n");
                kp->flagb = 1;
            }
        }
    }

    if (keyx == keyx_old) {
        if (key_param[0]) {
            if (keyx) {
                if (key_param[81] == 1 ) {
                    if(kp->flagx == 1)
                    do_swipe(kp, key_param[8], key_param[9], key_param[79],
                            key_param[80], 4);
                } else
                    key_report(kp, key_param[8], key_param[9], 4);
                kp->flagx = 0;
            } else {
                kp->flagx = 1;
            }
        } else if (keyx == kp->flagx) {
            if (keyx) {
                input_report_key(input, BUTTON_X, 1);
                input_mt_sync(input);
                kp->flagx = 0;
                printk("KEY X\n");
            } else {
                input_report_key(input, BUTTON_X, 0);
                input_mt_sync(input);
                kp->flagx = 1;
                printk("KEY X release\n");
            }
        }
    }

    if (keyy == keyy_old) {
        if (key_param[0]) {
            if (keyy) {
                if (key_param[84] == 1 ) {
                    if(kp->flagy == 1)
                    do_swipe(kp, key_param[10], key_param[11], key_param[82],
                            key_param[83], 5);
                } else
                    key_report(kp, key_param[10], key_param[11], 5);
                kp->flagy = 0;
            } else {
                kp->flagy = 1;
            }
        } else if (keyy == kp->flagy) {
            if (keyy) {
                input_report_key(input, BUTTON_Y, 1);
                input_mt_sync(input);
                kp->flagy = 0;
                printk("KEY Y\n");
            } else {
                input_report_key(input, BUTTON_Y, 0);
                input_mt_sync(input);
                kp->flagy = 1;
                printk("KEY Y release\n");
            }
        }
    }

    keyl_old = keyl;
    keyr_old = keyr;
    keyl2_old = keyl2;
    keyr2_old = keyr2;
    keya_old = keya;
    keyb_old = keyb;
    keyx_old = keyx;
    keyy_old = keyy;
}

static ssize_t key_read(struct device *dev, struct device_attribute *attr,
        char *buf) {
    char i;
    for (i = 0; i < 109; i++) {
        printk("key_param[%d] = %d \n", i, key_param[i]);
    }
    return 0;
}

static ssize_t key_write(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count) {
    sscanf(buf,
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld"
            "%ld %ld %ld %ld %ld %ld %ld %ld %ld "
                        ,

            &key_param[0], &key_param[1], &key_param[2], &key_param[3],
            &key_param[4], &key_param[5], &key_param[6], &key_param[7],
            &key_param[8], &key_param[9], &key_param[10], &key_param[11],
            &key_param[12], &key_param[13], &key_param[14], &key_param[15],
            &key_param[16], &key_param[17], &key_param[18], &key_param[19],
            &key_param[20], &key_param[21], &key_param[22], &key_param[23],
            &key_param[24], &key_param[25], &key_param[26], &key_param[27],
            &key_param[28], &key_param[29], &key_param[30], &key_param[31],
            &key_param[32], &key_param[33], &key_param[34], &key_param[35],
            &key_param[36], &key_param[37], &key_param[38], &key_param[39],
            &key_param[40], &key_param[41], &key_param[42], &key_param[43],
            &key_param[44], &key_param[45], &key_param[46], &key_param[47],
            &key_param[48], &key_param[49], &key_param[50], &key_param[51],
            &key_param[52], &key_param[53], &key_param[54], &key_param[55],
            &key_param[56], &key_param[57], &key_param[58], &key_param[59],
            &key_param[60], &key_param[61], &key_param[62], &key_param[63],
            &key_param[64], &key_param[65], &key_param[66], &key_param[67],
            &key_param[68], &key_param[69], &key_param[70], &key_param[71],
            &key_param[72], &key_param[73], &key_param[74], &key_param[75],
            &key_param[76], &key_param[77], &key_param[78], &key_param[79],
            &key_param[80], &key_param[81], &key_param[82], &key_param[83],
            &key_param[84], &key_param[85], &key_param[86], &key_param[87],
            &key_param[88], &key_param[89], &key_param[90], &key_param[91],
            &key_param[92], &key_param[93], &key_param[94], &key_param[95],
            &key_param[96], &key_param[97], &key_param[98], &key_param[99],
            &key_param[100], &key_param[101], &key_param[102], &key_param[103],
            &key_param[104], &key_param[105], &key_param[106], &key_param[107],
            &key_param[108]);
    if (key_param[0] == 1 || (key_param[0] == 3 && key_param[32] == 1)) {
        printk("------- mode 1 -------\n");
        if (key_param[20] == 0)
            key_param[20] = X3;

        if (key_param[20] == X1)
            key_param[20] = VX1;
        else if (key_param[20] == X2)
            key_param[20] = VX2;
        else if (key_param[20] == X3)
            key_param[20] = VX3;
        else if (key_param[20] == X4)
            key_param[20] = VX4;
        else if (key_param[20] == X5)
            key_param[20] = VX5;

        if (key_param[21] == 0 || key_param[22] == 0) {
            key_param[21] = (LCD_SCREEN_X + XCENTER) / 2;
            key_param[22] = LCD_SCREEN_Y / 2;
        }
    }
    if (key_param[0] == 2) {
        printk("------- mode 2 -------\n");
    }
    if(key_param[0]==3){
        printk("------- mode 3 -------\n");
    }
    int i;

    return count;
}
static DEVICE_ATTR(key, S_IRWXUGO, key_read, key_write);

static struct attribute *key_attr[] = { &dev_attr_key.attr, NULL };
static struct attribute_group key_attr_group =
        { .name = NULL, .attrs = key_attr, };

static int view_count;
static long int x, y;

static void circle_move(struct kp *kp) {
    int tmp_x = 0;
    int tmp_y = 0;
    int dx = 0, dy = 0;

    if (view_count < key_param[20]) {
        x += kp->key_value[1];
        y += kp->key_value[0];
        kp->key_value[1] = kp->old_x;
        kp->key_value[0] = kp->old_y;
        view_count++;
        return;
    } else {
        x /= key_param[20];
        y /= key_param[20];
        view_count = 0;
    }

    if (512 - x >= 0) {
        if (x < STEP8)
            tmp_x = kp->old_x + VALUE8;
        else if (x < STEP7)
            tmp_x = kp->old_x + VALUE7;
        else if (x < STEP6)
            tmp_x = kp->old_x + VALUE6;
        else if (x < STEP5)
            tmp_x = kp->old_x + VALUE5;
        else if (x < STEP4)
            tmp_x = kp->old_x + VALUE4;
        else if (x < STEP3)
            tmp_x = kp->old_x + VALUE3;
        else if (x < STEP2)
            tmp_x = kp->old_x + VALUE2;
        else if (x < STEP1)
            tmp_x = kp->old_x + VALUE1;
        else
            tmp_x = kp->old_x;
    } else {
        dx = 1024 - x;
        if (dx < STEP8)
            tmp_x = kp->old_x - VALUE8;
        else if (dx < STEP7)
            tmp_x = kp->old_x - VALUE7;
        else if (dx < STEP6)
            tmp_x = kp->old_x - VALUE6;
        else if (dx < STEP5)
            tmp_x = kp->old_x - VALUE5;
        else if (dx < STEP4)
            tmp_x = kp->old_x - VALUE4;
        else if (dx < STEP3)
            tmp_x = kp->old_x - VALUE3;
        else if (dx < STEP2)
            tmp_x = kp->old_x - VALUE2;
        else if (dx < STEP1)
            tmp_x = kp->old_x - VALUE1;
        else
            tmp_x = kp->old_x;
    }

    if (y - 512 >= 0) {
        dy = 1024 - y;
        if (dy < STEP8)
            tmp_y = kp->old_y + VALUE8;
        else if (dy < STEP7)
            tmp_y = kp->old_y + VALUE7;
        else if (dy < STEP6)
            tmp_y = kp->old_y + VALUE6;
        else if (dy < STEP5)
            tmp_y = kp->old_y + VALUE5;
        else if (dy < STEP4)
            tmp_y = kp->old_y + VALUE4;
        else if (dy < STEP3)
            tmp_y = kp->old_y + VALUE3;
        else if (dy < STEP2)
            tmp_y = kp->old_y + VALUE2;
        else if (dy < STEP1)
            tmp_y = kp->old_y + VALUE1;
        else
            tmp_y = kp->old_y;
    } else {
        if (y < STEP8)
            tmp_y = kp->old_y - VALUE8;
        else if (y < STEP7)
            tmp_y = kp->old_y - VALUE7;
        else if (y < STEP6)
            tmp_y = kp->old_y - VALUE6;
        else if (y < STEP5)
            tmp_y = kp->old_y - VALUE5;
        else if (y < STEP4)
            tmp_y = kp->old_y - VALUE4;
        else if (y < STEP3)
            tmp_y = kp->old_y - VALUE3;
        else if (y < STEP2)
            tmp_y = kp->old_y - VALUE2;
        else if (y < STEP1)
            tmp_y = kp->old_y - VALUE1;
        else
            tmp_y = kp->old_y;
    }

    if (tmp_x > LCD_SCREEN_X) {
        tmp_x = LCD_SCREEN_X;
        tmp_y = kp->old_y;
    }
    if (tmp_x < 0) {
        tmp_x = 0;
        tmp_y = kp->old_y;
    }
    if (tmp_y > LCD_SCREEN_Y) {
        tmp_y = LCD_SCREEN_Y;
        tmp_x = kp->old_x;
    }
    if (tmp_y < 0) {
        tmp_y = 0;
        tmp_x = kp->old_x;
    }

    /*
     if (tmp_x > LCD_SCREEN_X || tmp_x < 0 || tmp_y > LCD_SCREEN_Y || tmp_y < 0) {
     //tmp_x = (LCD_SCREEN_X+XCENTER) / 2;
     //tmp_y = LCD_SCREEN_Y / 2;
     tmp_x = key_param[21];
     tmp_y = key_param[22];
     }
     */

    kp->key_value[1] = tmp_x;
    kp->key_value[0] = tmp_y;
    kp->old_x = tmp_x;
    kp->old_y = tmp_y;
    x = 0;
    y = 0;
}

static void kp_work(struct kp *kp) {
    int i, code, value;
    kp_search_key(kp);

    //Channel 4 statement has been removed since we want it to work only when virtual mapping is off,
    //it is now included in if(key_param[0]==0) for, we raised kb->chan_num to 5

    if (key_param[0] == 1 || (key_param[0] == 3 && key_param[32] == 1)) {
        //circle 1 (right joystick)
        if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {
            kp->circle_flag[1] = 1;
            if (!second1) {
                //key_report(kp, (LCD_SCREEN_X+XCENTER)/2, LCD_SCREEN_Y/2, 1);
                key_report(kp, key_param[21], key_param[22], 1);
                second1 = 1;
            } else {
                circle_move(kp);
                key_report(kp, kp->key_value[1], kp->key_value[0], 1);
            }
        } else if (kp->circle_flag[1] == 1) {
            kp->circle_flag[1] = 0;
            second1 = 0;
            //kp->old_x = (LCD_SCREEN_X+XCENTER)/2;
            //kp->old_y = LCD_SCREEN_Y/2;
            kp->old_x = key_param[21];
            kp->old_y = key_param[22];
        }
    }

    if (key_param[0] == 2 || (key_param[0] == 3 && key_param[32] == 2)) {
        //circle 1 (right joystick)
        if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {
            kp->circle_flag[1] = 1;
            if (second1 < CENTER_TRY) {
                if (second1 == 0)
                    key_report(kp, key_param[20], key_param[21], 1);
                if (second1 == 1)
                    key_report(kp, key_param[20] + 1, key_param[21], 1);
                if (second1 == 2)
                    key_report(kp, key_param[20], key_param[21] + 1, 1);
                if (second1 == 3)
                    key_report(kp, key_param[20] - 1, key_param[21], 1);
                if (second1 == 4)
                    key_report(kp, key_param[20], key_param[21] - 1, 1);
                second1++;
            } else {
                key_report(kp,
                        key_param[20]
                                + (512 - kp->key_value[1]) * key_param[22]
                                        / 512,
                        key_param[21]
                                + (kp->key_value[0] - 512) * key_param[22]
                                        / 512, 1);
            }
        } else if (kp->circle_flag[1] == 1) {
            kp->circle_flag[1] = 0;
            second1 = 0;
        }
    }

    if (key_param[0] == 3 && key_param[32] == 3) {
        //circle 1 (right joystick SPLIT)
        if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {

            if (kp->key_valid[0] == 1) {
                value = kp->key_value[0];
                if (value >= 0 && value <= 1023 / 2 - ADC_VALUE * 2) {
                    if (key_param[63] == 1) {
                        if(kp->circle_flag[1]==0)
                        do_swipe(kp, key_param[41], key_param[42],
                                key_param[61], key_param[62], 13);
                    } else
                        key_report(kp, key_param[41], key_param[42], 13);
                } else if (value >= 1023 / 2 + ADC_VALUE * 2) {
                    if (key_param[69] == 1 ) {
                        if(kp->circle_flag[1]==0)
                        do_swipe(kp, key_param[45], key_param[46],
                                key_param[67], key_param[68], 13);
                    } else
                        key_report(kp, key_param[45], key_param[46], 13);
                }
            }
            if (kp->key_valid[1] == 1) {
                value = kp->key_value[1];
                if (value >= 0 && value <= 1023 / 2 - ADC_VALUE * 2) {
                    if (key_param[72] == 1) {
                        if(kp->circle_flag[1]==0)
                        do_swipe(kp, key_param[47], key_param[48],
                                key_param[70], key_param[71], 12);
                    } else
                        key_report(kp, key_param[47], key_param[48], 12);
                } else if (value >= 1023 / 2 + ADC_VALUE * 2) {
                    if (key_param[66] == 1 ) {
                        if(kp->circle_flag[1]==0)
                        do_swipe(kp, key_param[43], key_param[44],
                                key_param[64], key_param[65], 12);
                    } else
                        key_report(kp, key_param[43], key_param[44], 12);
                }
            }
            kp->circle_flag[1] = 1;
        } else if (kp->circle_flag[1] == 1) {
            kp->circle_flag[1] = 0;
        }
    }
    /*
     if (key_param[0] == 3 && key_param[32] == 4) {
     //circle 1 (right joystick "joystick" mode)
     if ((kp->key_valid[0] == 1) || (kp->key_valid[1] == 1)) {
     kp->circle_flag[1] = 1;
     if (!second1) {
     key_report(kp, (LCD_SCREEN_X + XCENTER) / 2, LCD_SCREEN_Y / 2,
     1);
     second1 = 1;
     } else {
     joystick_move(kp);
     key_report(kp, kp->key_value[1], kp->key_value[0], 1);
     }
     } else if (kp->circle_flag[1] == 1) {
     kp->circle_flag[1] = 0;
     second1 = 0;
     kp->old_x = CENTER_X;
     kp->old_y = CENTER_Y;
     }
     }
     */

    if (key_param[0] == 1 || key_param[0] == 2
            || (key_param[0] == 3 && key_param[31] == 2)) {
        //circle 0 (left joystick)
        if ((kp->key_valid[2] == 1) || (kp->key_valid[3] == 1)) {
            kp->circle_flag[0] = 1;
            if (second0 < CENTER_TRY) {
                if (second0 == 0)
                    key_report(kp, key_param[1], key_param[2], 0);
                if (second0 == 1)
                    key_report(kp, key_param[1] + 1, key_param[2], 0);
                if (second0 == 2)
                    key_report(kp, key_param[1], key_param[2] + 1, 0);
                if (second0 == 3)
                    key_report(kp, key_param[1] - 1, key_param[2], 0);
                if (second0 == 4)
                    key_report(kp, key_param[1], key_param[2] - 1, 0);
                second0++;
            } else {
                key_report(kp,
                        key_param[1]
                                + (512 - kp->key_value[2]) * key_param[3] / 512,
                        key_param[2]
                                + (512 - kp->key_value[3]) * key_param[3] / 512,
                        0);
            }
        } else if (kp->circle_flag[0] == 1) {
            kp->circle_flag[0] = 0;
            second0 = 0;
        }
    }

    if (key_param[0] == 3 && key_param[31] == 3) {

        //circle 0 (left joystick SPLIT)
        if ((kp->key_valid[2] == 1) || (kp->key_valid[3] == 1)) {


            if (kp->key_valid[2] == 1) {
                value = kp->key_value[2];

                if (value >= 0 && value <= 1023 / 2 - ADC_VALUE * 2) {
                    if (key_param[60] == 1) {
                        if(kp->circle_flag[0]==0)
                        do_swipe(kp, key_param[39], key_param[40],
                                key_param[58], key_param[59], 14);
                    } else
                        key_report(kp, key_param[39], key_param[40], 14);
                } else if (value >= 1023 / 2 + ADC_VALUE * 2) {
                    if (key_param[54] == 1  ) {
                        if(kp->circle_flag[0]==0)
                        do_swipe(kp, key_param[35], key_param[36],
                                key_param[52], key_param[53], 14);
                    } else
                        key_report(kp, key_param[35], key_param[36], 14);
                }
            }
            if (kp->key_valid[3] == 1) {
                value = kp->key_value[3];

                if (value >= 0 && value <= 1023 / 2 - ADC_VALUE * 2) {
                    if (key_param[57] == 1  ) {
                        if(kp->circle_flag[0]==0)
                        do_swipe(kp, key_param[37], key_param[38],
                                key_param[55], key_param[56], 15);
                    } else
                        key_report(kp, key_param[37], key_param[38], 15);
                } else if (value >= 1023 / 2 + ADC_VALUE * 2) {
                    if (key_param[51] == 1  ) {
                        if(kp->circle_flag[0]==0)
                        do_swipe(kp, key_param[33], key_param[34],
                                key_param[49], key_param[50], 15);
                    } else
                        key_report(kp, key_param[33], key_param[34], 15);
                }
            }
            kp->circle_flag[0] = 1;
        } else if (kp->circle_flag[0] == 1) {
            kp->circle_flag[0] = 0;
        }
    }

    // Volume / start / select buttons
    //At the moment they're mapped as A B buttons, because we need to extend
    //Key params size. I planned an array sized 56-57 to get all the features.
    if (key_param[0] == 3) {
        if (kp->key_valid[4] == 1) {
            value = kp->key_value[4];

            if (value >= 0 && value <= (9 + 40)) {
                if (key_param[99] == 1 ) {
                    if(kp->flagchan4==1)
                    do_swipe(kp, key_param[25], key_param[26], key_param[97],
                            key_param[98], 12);
                } else
                    key_report(kp, key_param[25], key_param[26], 12);
            } else if (value >= 392 - 40 && value <= (392 + 40)) {
                if (key_param[102] == 1 ) {
                    if(kp->flagchan4==1)
                    do_swipe(kp, key_param[23], key_param[24], key_param[100],
                            key_param[101], 12);
                } else
                    key_report(kp, key_param[23], key_param[24], 12);
            } else if (value >= 275 - 40 && value <= 275 + 40) {
                if (key_param[105] == 1  ) {
                    if(kp->flagchan4==1)
                    do_swipe(kp, key_param[27], key_param[28], key_param[103],
                            key_param[104], 12);
                } else
                    key_report(kp, key_param[27], key_param[28], 12);
            } else if (value >= 150 - 40 && value <= 150 + 40) {
                if (key_param[108] == 1 ) {
                    if(kp->flagchan4==1)
                    do_swipe(kp, key_param[29], key_param[30], key_param[106],
                            key_param[107], 12);
                } else
                    key_report(kp, key_param[29], key_param[30], 12);
            }
            kp->flagchan4 = 0;
        } else if (kp->flagchan4 == 0){
            kp->flagchan4 = 1;
        }
    }

    scan_keys(kp);
    input_sync(kp->input);

    if (release && (kp->circle_flag[0] == 0) && (kp->circle_flag[1] == 0)
            && kp->flaga && kp->flagb && kp->flagx && kp->flagy && kp->flagl
            && kp->flagr && kp->flagl2 && kp->flagr2 && kp->flagchan4) {
        release = 0;
        input_report_key(kp->input, BTN_TOUCH, 0);
        input_report_abs(kp->input, ABS_MT_TOUCH_MAJOR, 0);
        input_report_abs(kp->input, ABS_MT_WIDTH_MAJOR, 0);
        input_mt_sync(kp->input);
        input_sync(kp->input);
        //printk("-------------- release all point -----------------\n");
    }

    if (key_param[0] == 0) {
        //left,right,up,down
        //Vektor Note: Change this i to 0 to detect input on all the channels!
        for (i = 0; i < kp->chan_num; i++) {
            code = kp->key_code[i];
            if (!code && !kp->cur_keycode[i]) {
                continue;
            } else if (code != kp->tmp_code[i]) {
                kp->tmp_code[i] = code;
                kp->count[i] = 0;
            } else if (++kp->count[i] == 2) {
                if (kp->cur_keycode[i] != code) {
                    if (!code) {
                        kp->cur_keycode_status[i] = 0;
                        //printk("key %d up\n", kp->cur_keycode[i]);
                        input_report_key(kp->input, kp->cur_keycode[i], 0);
                        kp->cur_keycode[i] = code;
                    } else if (kp->cur_keycode_status[i] == 1) {
                        //printk("--------------------------- error cur = %d  new code = %d -------------------------\n", kp->cur_keycode[i], code);
                        //printk("key %d up(force)\n", kp->cur_keycode[i]);
                        input_report_key(kp->input, kp->cur_keycode[i], 0);
                        kp->cur_keycode_status[i] = 0;
                        kp->count[i] = 0;
                    } else {
                        kp->cur_keycode_status[i] = 1;
                        //printk("key %d down\n", code);
                        input_report_key(kp->input, code, 1);
                        kp->cur_keycode[i] = code;
                    }
                }
            }
        }
        scan_keys(kp);
        input_sync(kp->input);
    }
}

static void update_work_func(struct work_struct *work) {
    struct kp
    *kp_data = container_of(work, struct kp, work_update);

    kp_work(kp_data);
}

static void kp_timer_sr(unsigned long data) {
    struct kp *kp_data = (struct kp *) data;
    schedule_work(&(kp_data->work_update));
    mod_timer(&kp_data->timer, jiffies + msecs_to_jiffies(10));
}

static int adckpd_config_open(struct inode *inode, struct file *file) {
    file->private_data = gp_kp;
    return 0;
}

static int adckpd_config_release(struct inode *inode, struct file *file) {
    file->private_data = NULL;
    return 0;
}

static const struct file_operations keypad_fops = { .owner = THIS_MODULE,
        .open = adckpd_config_open, .release = adckpd_config_release, };

static int register_keypad_dev(struct kp *kp) {
    int ret = 0;
    strcpy(kp->config_name, "am_adc_js");
    ret = register_chrdev(0, kp->config_name, &keypad_fops);
    if (ret <= 0) {
        printk("register char device error\r\n");
        return ret;
    }
    kp->config_major = ret;
    printk("adc keypad major:%d\r\n", ret);
    kp->config_class = class_create(THIS_MODULE,kp->config_name);
    kp->config_dev = device_create(kp->config_class, NULL,
            MKDEV(kp->config_major, 0), NULL, kp->config_name);

    return ret;
}

static int __devinit kp_probe(struct platform_device *pdev) {
    struct kp *kp;
    struct input_dev *input_dev;
    int i, ret;
    struct adc_kp_platform_data *pdata = pdev->dev.platform_data;
    s8 phys[32];

    if (!pdata) {
        dev_err(&pdev->dev, "platform data is required!\n");
        return -EINVAL;
    }

    kp = kzalloc(sizeof(struct kp), GFP_KERNEL);
    input_dev = input_allocate_device();
    if (!kp || !input_dev) {
        kfree(kp);
        input_free_device(input_dev);
        return -ENOMEM;
    }
    gp_kp = kp;

    platform_set_drvdata(pdev, kp);
    kp->input = input_dev;
    for (i = 0; i < SARADC_CHAN_NUM; i++) {
        kp->cur_keycode[i] = 0;
        kp->cur_keycode_status[i] = 0;
        kp->tmp_code[i] = 0;
        kp->count[i] = 0;
    }
    kp->flaga = kp->flagb = kp->flagx = kp->flagy = kp->flagl = kp->flagr =
            kp->flagl2 = kp->flagr2 = kp->flagchan4 = 0;
    kp->circle_flag[0] = 0;
    kp->circle_flag[1] == 0;

    //kp->old_x = (LCD_SCREEN_X+XCENTER)/2;
    //kp->old_y = LCD_SCREEN_Y/2;
    kp->old_x = key_param[21];
    kp->old_y = key_param[22];

    INIT_WORK(&(kp->work_update), update_work_func);

    setup_timer(&kp->timer, kp_timer_sr, kp);
    mod_timer(&kp->timer, jiffies + msecs_to_jiffies(100));

    /* setup input device */
    set_bit(KEY_SPACE, input_dev->keybit);
    set_bit(KEY_ENTER, input_dev->keybit);
    set_bit(KEY_VOLUMEDOWN, input_dev->keybit);
    set_bit(KEY_VOLUMEUP, input_dev->keybit);
    set_bit(KEY_UP, input_dev->keybit);
    set_bit(KEY_DOWN, input_dev->keybit);
    set_bit(KEY_LEFT, input_dev->keybit);
    set_bit(KEY_RIGHT, input_dev->keybit);
    set_bit(BUTTON_A, input_dev->keybit);
    set_bit(BUTTON_B, input_dev->keybit);
    set_bit(BUTTON_X, input_dev->keybit);
    set_bit(BUTTON_Y, input_dev->keybit);
    set_bit(BUTTON_L, input_dev->keybit);
    set_bit(BUTTON_R, input_dev->keybit);
    set_bit(BUTTON_L2, input_dev->keybit);
    set_bit(BUTTON_R2, input_dev->keybit);

    //VEKTOR: Here I add the keys used for RSTICK I J K L

    set_bit(KEY_I, input_dev->keybit);
    set_bit(KEY_J, input_dev->keybit);
    set_bit(KEY_K, input_dev->keybit);
    set_bit(KEY_L, input_dev->keybit);

    set_bit(EV_REP, input_dev->evbit);
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(EV_ABS, input_dev->evbit);
    set_bit(BTN_TOUCH, input_dev->keybit);
    set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
    set_bit(ABS_MT_POSITION_X, input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
    set_bit(ABS_MT_TRACKING_ID, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_X, 0, LCD_SCREEN_X, 0, 0);
    input_set_abs_params(input_dev, ABS_Y, 0, LCD_SCREEN_Y, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, LCD_SCREEN_X, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, LCD_SCREEN_Y, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, TRACKING_ID, 0, 0);

    kp->chan_num = 5; //Setting this to 5 allows us to use virtual mapping for volume/select/start too.
    kp->chan[0] = CHAN_0; //RSTICK UP, DOWN
    kp->chan[1] = CHAN_1; //RSTICK LEFT, RIGHT
    kp->chan[2] = CHAN_2; //LSTICK LEFT, RIGHT
    kp->chan[3] = CHAN_3; //LSTICK UP, DOWN
    kp->chan[4] = CHAN_4; //KEY_SPACE,KEY_ENTER,KEY_VOLUMEDOWN,KEY_VOLUMEUP

    sprintf(phys, "input/ts");
    input_dev->name = "adc joystick";
    input_dev->phys = phys;
    input_dev->dev.parent = &pdev->dev;

    input_dev->id.bustype = BUS_ISA;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = 0x0001;
    input_dev->id.version = 0x100;
    /*
     input_dev->id.bustype = BUS_ADB;
     input_dev->id.vendor = 0x222a;
     input_dev->id.product = 0x0001;
     input_dev->id.version = 0x0001;
     */

    input_dev->rep[REP_DELAY] = 0xffffffff;
    input_dev->rep[REP_PERIOD] = 0xffffffff;

    input_dev->keycodesize = sizeof(unsigned short);
    input_dev->keycodemax = 0x1ff;

    ret = input_register_device(kp->input);
    if (ret < 0) {
        printk(KERN_ERR "Unable to register keypad input device.\n");
        kfree(kp);
        input_free_device(input_dev);
        return -EINVAL;
    }
    printk("adc joystick register input device completed.\r\n");
    register_keypad_dev(gp_kp);

    gpio_keys_init();

    struct device *dev = &pdev->dev;
    sysfs_create_group(&dev->kobj, &key_attr_group);

    return 0;
}

static int kp_remove(struct platform_device *pdev) {
    struct kp *kp = platform_get_drvdata(pdev);

    input_unregister_device(kp->input);
    input_free_device(kp->input);
    unregister_chrdev(kp->config_major, kp->config_name);
    if (kp->config_class) {
        if (kp->config_dev)
            device_destroy(kp->config_class, MKDEV(kp->config_major, 0));
        class_destroy(kp->config_class);
    }
    kfree(kp);
    gp_kp = NULL;
    return 0;
}

static struct platform_driver kp_driver = { .probe = kp_probe, .remove =
        kp_remove, .suspend = NULL, .resume = NULL, .driver = { .name =
        "mx-adcjs", }, };

static int __devinit kp_init(void) {
    printk(KERN_INFO "ADC joystick Driver init.\n");
    return platform_driver_register(&kp_driver);
}

static void __exit kp_exit(void) {
    printk(KERN_INFO "ADC joystick Driver exit.\n");
    platform_driver_unregister(&kp_driver);
}

module_init(kp_init)
;
module_exit(kp_exit)
;

MODULE_AUTHOR("Robin Zhu");
MODULE_DESCRIPTION("ADC Joystick Driver");
MODULE_LICENSE("GPL");
