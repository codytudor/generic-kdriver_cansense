/*
 * CAN bus termination hardware status
 *
 * Copyleft 2016 Tudor Design Systems, LLC.
 *
 * Author: Cody Tudor <cody.tudor@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>     
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/string.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/sysfs.h>

enum bustermon_bits {
    CAN0 = 0,
    CAN1,
    MAX_CANS,
};

struct bustermon_platform_data {
    unsigned int gpios[MAX_CANS];   // array of gpios where index = canx termination
    char name[PLATFORM_NAME_SIZE];
};

struct bustermon_data {

    struct device *hwmon_dev;
    struct bustermon_platform_data *pdata;
    struct device *dev;    
    int use_count;
};

const char *const can_names[] = {
    [CAN0]      = "can0",
    [CAN1]      = "can1",
    [MAX_CANS]  = "undef",
};

static ssize_t bustermon_show_state(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    unsigned int state = 0;
    int cntr;
    struct bustermon_platform_data *data = dev_get_drvdata(dev);
    
    for (cntr = CAN0; cntr < MAX_CANS; cntr++) {
        if (strncmp(attr->attr.name, can_names[cntr], strlen(can_names[cntr])) == 0)
            state = (gpio_get_value(data->gpios[cntr])) ? 1 : 0;
    }
   
    return sprintf(buf, "%u\n", state);
}

static ssize_t bustermon_show_status(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    char *status = "";
    int cntr;
    int match = MAX_CANS;
    struct bustermon_platform_data *data = dev_get_drvdata(dev);
       
    for (cntr = CAN0; cntr < MAX_CANS; cntr++) {
        if (strncmp(attr->attr.name, can_names[cntr], strlen(can_names[cntr])) == 0) {
            status = (gpio_get_value(data->gpios[cntr])) ? "ON" : "OFF";
            match = cntr;
        }
    }
    
    return sprintf(buf, "%s termination is: %s\n", can_names[match], status);
}

static ssize_t bustermon_show_name(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    struct bustermon_platform_data *data = dev_get_drvdata(dev);

    return sprintf(buf, "%s\n", data->name);
}

static DEVICE_ATTR(can0_status, S_IRUGO, bustermon_show_status, NULL);
static DEVICE_ATTR(can1_status, S_IRUGO, bustermon_show_status, NULL);
static DEVICE_ATTR(can0_value, S_IRUGO, bustermon_show_state, NULL);
static DEVICE_ATTR(can1_value, S_IRUGO, bustermon_show_state, NULL);
static DEVICE_ATTR(name, S_IRUGO, bustermon_show_name, NULL);

static struct of_device_id bustermon_of_match[] = {
    { .compatible = "can-hwmon" },
    { }
};

MODULE_DEVICE_TABLE(of, bustermon_of_match);

static struct bustermon_platform_data *bustermon_parse_dt(struct platform_device *pdev)
{  
    struct device_node *node = pdev->dev.of_node;
    struct bustermon_platform_data *pdata;
    int length;
    int index;
    unsigned int gpio_num;
    int cntr;
    int retval = 0;

    if (!node)
        return ERR_PTR(-ENODEV);
        
    length = of_count_phandle_with_args(node, "gpios", "#gpio-cells");
    
    if (length < 1) {
        dev_err(&pdev->dev, "you need to define at least one gpio...\n"); 
        return ERR_PTR(-EINVAL);
    }
    
    if (of_property_count_strings(node, "gpio-names") != of_count_phandle_with_args(node, "gpios", "#gpio-cells")) {
        dev_err(&pdev->dev, "you need one name in gpio-names per triple in gpios...\n");
        return ERR_PTR(-ENODATA); 
    }
    
    pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
    if (!pdata)
        return ERR_PTR(-ENOMEM);

    memset(pdata, 0, sizeof(*pdata));
    
    for (cntr = CAN0; cntr < MAX_CANS; cntr++) {
        index = of_property_match_string(node, "gpio-names", can_names[cntr]);
        if (index >= 0) {
            gpio_num = of_get_named_gpio_flags(node, "gpios", index, NULL);
            retval = gpio_request(gpio_num, can_names[cntr]);
            if (retval < 0)
                goto err;
            retval = gpio_direction_input(gpio_num);
            if (retval < 0) {
                gpio_free(gpio_num);
                goto err;
            }
            pdata->gpios[cntr] = gpio_num;           
            dev_dbg(&pdev->dev, "found %s termination monitor pin\n", can_names[cntr]);
            index = -ENODATA;
        }
        else {
            dev_err(&pdev->dev, "couldn't find a matching name for %s\n", can_names[cntr]); 
            goto err;
        }
    }

    return pdata;
    
err:

    for (cntr = CAN0; cntr < MAX_CANS; cntr++) {
        if (pdata->gpios[cntr] > 0)
            gpio_free(pdata->gpios[cntr]);   
    }
    kfree(pdata);
    return ERR_PTR(retval);
}

static int bustermon_dt_probe(struct platform_device *pdev)
{
    struct bustermon_data *data;
    struct bustermon_platform_data *pdata;
    int ret;
    
    pdata = bustermon_parse_dt(pdev);
    
    if (IS_ERR(pdata))
        return PTR_ERR(pdata);
    else if (pdata == NULL)
        pdata = pdev->dev.platform_data;

    if (!pdata) {
        dev_err(&pdev->dev, "No platform init data supplied.\n");
        return -ENODEV;
    }
  
    data = devm_kzalloc(&pdev->dev, sizeof(struct bustermon_data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
        
    data->dev = &pdev->dev;
    data->pdata = pdata;
    strlcpy(pdata->name, dev_name(&pdev->dev), sizeof(pdata->name));
    
    platform_set_drvdata(pdev, data);

    data->hwmon_dev = hwmon_device_register(data->dev);
    if (IS_ERR(data->hwmon_dev)) {
        dev_err(data->dev, "failed to register can-hwmon driver\n");
        return PTR_ERR(data->hwmon_dev);
    }
    
    dev_set_drvdata(data->hwmon_dev, pdata);
    
    ret = device_create_file(data->hwmon_dev, &dev_attr_name);
    if (ret) {
        dev_err(data->dev, "unable to create dev_attr_name sysfs file\n");
        goto err_free_mem;
    }
    
    ret = device_create_file(data->hwmon_dev, &dev_attr_can0_status);
    if (ret) {
        dev_err(data->dev, "unable to create dev_attr_can0_status sysfs file\n");
        goto unregister_name;
    }
    
    ret = device_create_file(data->hwmon_dev, &dev_attr_can1_status);
    if (ret) {
        dev_err(data->dev, "unable to create dev_attr_can1_status sysfs file\n");
        goto unregister_can0;
    }
        
    ret = device_create_file(data->hwmon_dev, &dev_attr_can0_value);
    if (ret) {
        dev_err(data->dev, "unable to create dev_attr_can0_value sysfs file\n");
        goto unregister_can1;
    }
    
    ret = device_create_file(data->hwmon_dev, &dev_attr_can1_value);
    if (ret) {
        dev_err(data->dev, "unable to create dev_attr_can1_value sysfs file\n");
        goto unregister_all;
    }

    dev_info(&pdev->dev, "can-hwmon successfully probed.\n");

    return 0;
    
unregister_all:
    device_remove_file(data->hwmon_dev, &dev_attr_can0_value);
    
unregister_can1:
    device_remove_file(data->hwmon_dev, &dev_attr_can1_status);
    
unregister_can0:
    device_remove_file(data->hwmon_dev, &dev_attr_can0_status);
    
unregister_name:
    device_remove_file(data->hwmon_dev, &dev_attr_name);
    
err_free_mem:
    hwmon_device_unregister(data->hwmon_dev);
    kfree(data);
    kfree(pdata);
    return ret;
    
}

static int bustermon_remove(struct platform_device *pdev)
{
    struct bustermon_data *data = platform_get_drvdata(pdev);

    device_remove_file(data->hwmon_dev, &dev_attr_name);
    device_remove_file(data->hwmon_dev, &dev_attr_can0_status);
    device_remove_file(data->hwmon_dev, &dev_attr_can1_status);
    hwmon_device_unregister(data->hwmon_dev);
    dev_set_drvdata(data->hwmon_dev, NULL);
    platform_set_drvdata(pdev, NULL);

    return 0;
}

static struct platform_driver bustermon_driver = {
    .driver     = {
        .name       = "can-hwmon",
        .owner      = THIS_MODULE,
        .of_match_table = of_match_ptr(bustermon_of_match),
    },
    .probe      = bustermon_dt_probe,
    .remove     = bustermon_remove,
};

module_platform_driver(bustermon_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cody Tudor <cody.tudor@gmail.com>");
MODULE_DESCRIPTION("CAN bus termination status");
MODULE_ALIAS("platform:can-hwmon");
