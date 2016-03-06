/*
 * @file userspace_access_test.c
 * @date 6 Mar 2016
 * @author Chester Gillon
 * @brief Test program to access a UIO device from userspace
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <linux/types.h>
#include "umem.h"

#define UIO_CLASS_ROOT "/sys/class/uio"

/** Contains the context of the NVRAM UIO device */
typedef struct
{
    /** The name of the NVRAM UIO device */
    char device_name[PATH_MAX];
    /** The offset required to be added to the mmap for access for the NVRAM device csr registers */
    unsigned int csr_mmap_offset;
    /** The size of the csr registers to be mapped */
    unsigned int csr_mmap_size;
    /** The file descriptor for the NVRAM UIO device */
    int device_fd;
    /** The base of mapped NVRAM device csr registers */
    volatile char *csr;
    /** Mapped to specific NVRAM device csr registers */
    volatile uint8_t *memctrlstatus_magic;
    volatile uint8_t *memctrlstatus_memory;
    volatile uint8_t *memctrlstatus_battery;
    volatile uint8_t *memctrlcmd_ledctrl;
    volatile uint8_t *memctrlcmd_errctrl;
} nvram_uio_context;

/**
 * @brief Find the UIO device entry for the NVRAM board
 * @param[out] context The NVRAM UIO device which has been found
 */
static void find_uio_device (nvram_uio_context *const context)
{
    DIR *uio_dir;
    struct dirent *entry;
    bool found_nvram_uio_device = false;
    char uio_param_pathname[PATH_MAX];
    FILE *uio_param_file;
    char uio_driver_name[80];

    memset (context, 0, sizeof (nvram_uio_context));
    uio_dir = opendir (UIO_CLASS_ROOT);
    if (uio_dir == NULL)
    {
        printf ("Failed to open %s\n", UIO_CLASS_ROOT);
        exit (EXIT_FAILURE);
    }

    entry = readdir (uio_dir);
    while ((entry != NULL) && !found_nvram_uio_device)
    {
        if ((entry->d_type == DT_DIR) || (entry->d_type == DT_LNK))
        {
            snprintf (uio_param_pathname, PATH_MAX, "%s/%s/name", UIO_CLASS_ROOT, entry->d_name);
            uio_param_file = fopen (uio_param_pathname, "r");
            if (uio_param_file != NULL)
            {
                found_nvram_uio_device = fgets (uio_driver_name, sizeof (uio_driver_name), uio_param_file) != NULL;
                if (found_nvram_uio_device)
                {
                    if (uio_driver_name[strlen(uio_driver_name) - 1] == '\n')
                    {
                        uio_driver_name[strlen(uio_driver_name) - 1] = '\0';
                    }
                    found_nvram_uio_device = strcmp (uio_driver_name, DRIVER_NAME) == 0;
                }
                fclose (uio_param_file);

                if (found_nvram_uio_device)
                {
                    strcpy (context->device_name, entry->d_name);
                }
            }
        }
        entry = readdir (uio_dir);
    }
    closedir (uio_dir);

    if (!found_nvram_uio_device)
    {
        printf ("Failed to find entry for %s under %s\n", DRIVER_NAME, UIO_CLASS_ROOT);
        exit (EXIT_FAILURE);
    }
}

/**
 * @brief Read the parameter value for one UIO device mapping
 * @param[in] device_name The name of the UIO device to read the parameter for
 * @param[in] mapping_index Which mapping to read the parameter for
 * @param[in] param_name The name of the parameter to read the value for
 * @return Returns the parameter value
 */
static unsigned int read_uio_mapping_param (const char *device_name, const unsigned int mapping_index, const char *param_name)
{
    char uio_param_pathname[PATH_MAX];
    FILE *uio_param_file;
    bool success;
    unsigned int param_value;

    snprintf (uio_param_pathname, PATH_MAX, "%s/%s/maps/map%u/%s", UIO_CLASS_ROOT, device_name, mapping_index, param_name);
    uio_param_file = fopen (uio_param_pathname, "r");
    success = uio_param_file != NULL;
    if (success)
    {
        success = fscanf (uio_param_file, "0x%x", &param_value) == 1;
        fclose (uio_param_file);
    }
    if (!success)
    {
        printf ("Failed to read value from %s\n", uio_param_pathname);
        exit (EXIT_FAILURE);
    }

    return param_value;
}

/**
 * @brief Get the NVRAM UIO device parameters required for operation
 * @param[in,out] context The NVRAM UIO device being opened
 */
static void get_uio_device_parameters (nvram_uio_context *const context)
{
    context->csr_mmap_offset = read_uio_mapping_param (context->device_name, CSR_MAPPING_INDEX, "offset");
    context->csr_mmap_size = read_uio_mapping_param (context->device_name, CSR_MAPPING_INDEX, "size");
}

/**
 * @brief Open the NVRAM UIO device, and map its csr registers
 * @param[in,out] context The NVRAM UIO device being opened
 */
static void open_uio_device (nvram_uio_context *const context)
{
    char device_pathname[PATH_MAX];

    snprintf (device_pathname, PATH_MAX, "/dev/%s", context->device_name);
    context->device_fd = open (device_pathname, O_RDWR);
    if (context->device_fd == -1)
    {
        printf ("Failed to open %s\n", device_pathname);
        perror (NULL);
        exit (EXIT_FAILURE);
    }

    context->csr = mmap (NULL, context->csr_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, context->device_fd,
                         CSR_MAPPING_INDEX * getpagesize ());
    if (context->csr == MAP_FAILED)
    {
        printf ("Failed to map csr registers for %s\n", context->device_name);
        perror (NULL);
        exit (EXIT_FAILURE);
    }
    context->csr += context->csr_mmap_offset;

    context->memctrlstatus_magic = (volatile uint8_t *) &context->csr[MEMCTRLSTATUS_MAGIC];
    context->memctrlstatus_memory = (volatile uint8_t *) &context->csr[MEMCTRLSTATUS_MEMORY];
    context->memctrlstatus_battery = (volatile uint8_t *) &context->csr[MEMCTRLSTATUS_BATTERY];
    context->memctrlcmd_ledctrl = (volatile uint8_t *) &context->csr[MEMCTRLCMD_LEDCTRL];
    context->memctrlcmd_errctrl = (volatile uint8_t *) &context->csr[MEMCTRLCMD_ERRCTRL];
}

/**
 * @brief Close the NVRAM UIO device
 * @param[in,out] context The NVRAM UIO device to close
 */
static void close_uio_device (nvram_uio_context *const context)
{
    int rc;

    rc = munmap ((void *) context->csr, context->csr_mmap_size);
    if (rc != 0)
    {
        printf ("Failed to munmap %s\n", context->device_name);
        exit (EXIT_FAILURE);
    }
    context->csr = NULL;

    close (context->device_fd);
}

/**
 * @brief Set a LED on the NVRAM board
 * @param[in] The NVRAM UIO device to set the led state for
 * @param[in] shift Identifies which led to set the state for
 * @param[in] state The state of the led to set
 */
static void set_led (nvram_uio_context *const context, int shift, unsigned char state)
{
    uint8_t led;

    led = *context->memctrlcmd_ledctrl;
    if (state == LED_FLIP)
    {
        led ^= (1<<shift);
    }
    else
    {
        led &= ~(0x03 << shift);
        led |= (state << shift);
    }
    *context->memctrlcmd_ledctrl = led;

}

int main (int argc, char *argv[])
{
    nvram_uio_context context;

    find_uio_device (&context);
    get_uio_device_parameters (&context);
    open_uio_device (&context);
    printf ("memctrlstatus_magic=0x%x\n", *context.memctrlstatus_magic);
    printf ("memctrlstatus_memory=0x%x\n", *context.memctrlstatus_memory);
    printf ("memctrlstatus_battery=0x%x\n", *context.memctrlstatus_battery);
    printf ("memctrlcmd_ledctrl=0x%x\n", *context.memctrlcmd_ledctrl);
    printf ("memctrlcmd_errctrl=0x%x\n", *context.memctrlcmd_errctrl);
    set_led (&context, LED_FAULT, LED_ON);
    set_led (&context, LED_FAULT, LED_FLASH_7_0);
    set_led (&context, LED_FAULT, LED_FLASH_3_5);
    set_led (&context, LED_FAULT, LED_OFF);
    close_uio_device (&context);

    return EXIT_SUCCESS;
}
