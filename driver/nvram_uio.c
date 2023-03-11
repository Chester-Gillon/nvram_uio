
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>

#include "umem.h"

static struct pci_device_id nvram_uio_pci_ids[] =
{
    {PCI_DEVICE(PCI_VENDOR_ID_MICRO_MEMORY, PCI_DEVICE_ID_MICRO_MEMORY_5425CN)},
    { 0, }
};

static irqreturn_t nvram_uio_handler (int irq, struct uio_info *dev_info)
{
    return IRQ_NONE;
}

static int nvram_uio_pci_probe (struct pci_dev *dev,
                                const struct pci_device_id *id)
{
    struct uio_info *info;
    unsigned long csr_base;
    unsigned long csr_len;
    int magic_number;
    int magic_numbers[MAGIC_NUMBERS_PER_DEV + 1];
    int magic_number_ok = 0;
    int i;

    info = kzalloc (sizeof(struct uio_info), GFP_KERNEL);
    if (info == NULL)
    {
        return -ENOMEM;
    }

    if (pci_enable_device(dev))
    {
        goto out_free;
    }

    pci_write_config_byte (dev, PCI_LATENCY_TIMER, 0xF8);
    pci_set_master (dev);

    dev_printk (KERN_INFO, &dev->dev,
      "Curtiss Wright controller found (PCI Mem Module (Battery Backup))\n");

    if (pci_set_dma_mask(dev, DMA_BIT_MASK(64)))
    {
        dev_printk (KERN_WARNING, &dev->dev, "NO suitable DMA found\n");
        return  -ENOMEM;
    }

    if (pci_request_regions (dev, DRIVER_NAME))
    {
        dev_printk (KERN_ERR, &dev->dev, "Unable to request memory region\n");
        goto out_disable;
    }

    csr_base = pci_resource_start (dev, 0);
    csr_len  = pci_resource_len (dev, 0);
    if (!csr_base || !csr_len)
    {
        goto out_release;
    }

    /* Need to page align the mapped length as otherwise mmap() in user space can fail with EINVAL */
    csr_len = ((csr_len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    info->mem[CSR_MAPPING_INDEX].addr = csr_base;
    info->mem[CSR_MAPPING_INDEX].internal_addr = ioremap_nocache (csr_base, csr_len);
    if (!info->mem[CSR_MAPPING_INDEX].internal_addr)
    {
        dev_printk (KERN_ERR, &dev->dev, "Unable to remap memory region\n");
        goto out_release;
    }
    info->mem[CSR_MAPPING_INDEX].size = csr_len;
    info->mem[CSR_MAPPING_INDEX].memtype = UIO_MEM_PHYS;
    info->mem[CSR_MAPPING_INDEX].name = "csr";

    dev_printk (KERN_INFO, &dev->dev, "CSR 0x%08llx -> 0x%p (0x%llx)\n",
            info->mem[CSR_MAPPING_INDEX].addr, info->mem[CSR_MAPPING_INDEX].internal_addr, info->mem[CSR_MAPPING_INDEX].size);

    switch (dev->device) {
    case 0x5415:
        magic_numbers[0] = 0x59;
        magic_numbers[1] = 0x100;
        break;

    case 0x5425:
        magic_numbers[0] = 0x5C;
        magic_numbers[1] = 0x5E;
        magic_numbers[2] = 0x100;
        break;

    case 0x6155:
        magic_numbers[0] = 0x99;
        magic_numbers[1] = 0x100;
        break;

    default:
        magic_numbers[0] = 0x100;
        break;
    }

    magic_number = readb(info->mem[0].internal_addr + MEMCTRLSTATUS_MAGIC);
    for (i = 0; i < MAGIC_NUMBERS_PER_DEV; i++) {
        if (magic_numbers[i] == magic_number) {
        magic_number_ok = 1;
        break;
        }
        if (magic_numbers[i] >= 0x100) break;
    }
    if (!magic_number_ok)
    {
        dev_printk (KERN_ERR, &dev->dev, "Magic number 0x%02x invalid for device 0x%04x\n", magic_number, dev->device);
        goto out_release;
    }
    info->name = DRIVER_NAME;
    info->version = "0.0.1";
    info->irq = dev->irq;
    info->irq_flags = IRQF_SHARED;
    info->handler = nvram_uio_handler;

    if (uio_register_device (&dev->dev, info))
    {
        goto out_unmap;
    }

    pci_set_drvdata (dev, info);

    return 0;

    out_unmap:
        iounmap(info->mem[0].internal_addr);
    out_release:
        pci_release_regions (dev);
    out_disable:
        pci_disable_device (dev);
    out_free:
        kfree (info);
        return -ENODEV;
}

static void nvram_uio_pci_remove (struct pci_dev *dev)
{
    struct uio_info *info = pci_get_drvdata(dev);

    uio_unregister_device (info);
    pci_release_regions (dev);
    pci_disable_device (dev);
    pci_set_drvdata (dev, NULL);
    iounmap (info->mem[0].internal_addr);

    kfree (info);
}

static struct pci_driver nvram_uio_pci_driver = {
    .name = DRIVER_NAME,
    .id_table = nvram_uio_pci_ids,
    .probe = nvram_uio_pci_probe,
    .remove = nvram_uio_pci_remove,
};
static int __init nvram_uio_init_module(void)
{
    return pci_register_driver(&nvram_uio_pci_driver);
}

static void __exit nvram_uio_exit_module(void)
{
    pci_unregister_driver(&nvram_uio_pci_driver);
}

module_init(nvram_uio_init_module);
module_exit(nvram_uio_exit_module);
MODULE_DEVICE_TABLE(pci, nvram_uio_pci_ids);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Chester Gillon");
