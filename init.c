#include <linux/pci.h>
#include <linux/init.h>
#include <asm/pci_x86.h>
#include <asm/x86_init.h>

/* arch_initcall has too random ordering, so call the initializers
   in the right sequence from here. */
/*Reconfig PCI*/
static __init void get_range(int bus, unsigned  int start, unsigned int *end)
{
	uint32_t  l;	int pcidev;
	int  oe = *end;
	uint32_t sec, sub;
	printk("OE: %x\n" , oe);
	*end = start;
	for(pcidev = 0; pcidev < 32; pcidev++){
#if 0
		raw_pci_read(0, bus, pcidev<<3, PCI_VENDOR_ID, 4, &l);
		if(l != 0x869610b5)/*PLX bridge*/{
			continue;
		}
		printk("BRIDGE FOUND at %d:%d\n", bus, pcidev);
#else
		/* Possibly accept nForce bridge*/
		raw_pci_read(0, bus, pcidev<<3, PCI_CLASS_DEVICE, 2, &l);
		if(l  != PCI_CLASS_BRIDGE_PCI)
			continue;
#endif
                raw_pci_read(0, bus, pcidev<<3, PCI_SECONDARY_BUS, 1, &sec);
                raw_pci_read(0, bus, pcidev<<3, PCI_SUBORDINATE_BUS, 1, &sub);

		/*Explicitly erase BAR 0*/
		raw_pci_write(0, bus, pcidev<<3, PCI_BASE_ADDRESS_0, 4, 0);
		if(sec != sub){
			/*Recursive call is not encouraged in Kernel.*/
			printk(KERN_ERR "Bus %d Under the BRIDGE %x %x\n"
			       ,sec ,start, *end);
			*end = oe;
			get_range(sec, start, end);
#if 0 
			/*Spacer Not needed.*/
			*end += 0x1000000;
#endif
		}else{
			raw_pci_read(0, sec, 0, PCI_VENDOR_ID,4, &l);
			if(l == 0xffffffff)
				continue;
			
			*end = start + 0xffffff;/*16M*/
#if 1
			if(*end >= oe){
				printk(KERN_ERR "Too large RANGE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
				*end = (start -1);
				return;
			}
#endif

			printk(KERN_ERR "BUS %d START:%x END%x\n",sec,
			       start, *end);
			/*Allocate BAR0 for GPU*/
			raw_pci_write(0, sec, 0,PCI_BASE_ADDRESS_0, 4, 
				      start);
			/*Deallocate ROM*/
			raw_pci_write(0, sec, 0, PCI_ROM_ADDRESS, 4, 0);
			/*Deallocate BAR0 for Audio*/
			raw_pci_write(0, sec, 1, PCI_BASE_ADDRESS_0, 4, 0);
		}
		printk("%x:%xBRIDGE RANGE REG %x\n", bus,
		       pcidev, (*end)&0xffff0000|(start>>16));
		raw_pci_write(0, bus, pcidev<<3, PCI_MEMORY_BASE,
			      4, ((*end)&0xffff0000)|(start>>16));

		start = *end + 1;
	}
}

static __init void config_top_bus(int bus, int dev, int func, u32 start, u32 end)
{
	u32 l;
	u32 sec;
	u32 devfunc = dev<<3|func;
	raw_pci_read(0, bus, devfunc,PCI_SECONDARY_BUS, 1, &sec);
	l = (start>>16)|(end&0xffff0000);
	
	raw_pci_write(0, bus, devfunc, PCI_MEMORY_BASE, 4, l);
	raw_pci_write(0, sec, 0, PCI_MEMORY_BASE, 4, l);
}
static __init void scan_bus()
{
	int pcibus, pcidevice;
	u32 start,end;
	int  sec,sub;
	u32 l;
	config_top_bus(0x80, 1, 0, 0xe0000000, 0xe7ffffff);
	config_top_bus(0x80, 2, 0, 0xe8000000, 0xefffffff);
	config_top_bus(0x0, 1, 0, 0x92000000, 0xafffffff);
	config_top_bus(0x0, 2, 0, 0xd0000000, 0xde3fffff);
	config_top_bus(0x80, 3, 2, 0xf0000000, 0xfbdfffff);
	
	for(pcibus = 0 ; pcibus < 256; pcibus++){
		raw_pci_read(0, pcibus, 0, PCI_VENDOR_ID, 4, &l);
		if(l != 0x869610b5)/*PLX bridge*/
			continue;
		printk(KERN_ERR "BRIDGE FOUND\n");
		raw_pci_read(0, pcibus, 0, PCI_CLASS_DEVICE, 2, &l);
		if(l  != PCI_CLASS_BRIDGE_PCI)
			continue;
		
		printk(KERN_ERR "PCIBUS:%d\n", pcibus);
		raw_pci_read(0, pcibus, 0, PCI_SECONDARY_BUS, 1, &sec);
		raw_pci_read(0, pcibus, 0, PCI_SUBORDINATE_BUS, 1, &sub);
		raw_pci_read(0, pcibus, 0,PCI_MEMORY_BASE, 4, &end);

		/*Explicitly erase BAR 0*/
		raw_pci_write(0, pcibus, 0, PCI_BASE_ADDRESS_0, 4, 0);

		start = (end &0xffff)<<16;
		end &= ~0xffff;
		
		if(sec != sub){
			printk(KERN_ERR "PCIBUS %d start%x end %x\n", 
			       pcibus, start,end);
			get_range(pcibus,  start, &end);
		}
		raw_pci_write(0, pcibus, 0, PCI_MEMORY_BASE, 4, (end&0xffff0000)|((start>>16)&0xffff));
		pcibus = sub ;
		
	}

}
static __init int pci_arch_init(void)
{
#ifdef CONFIG_PCI_DIRECT
	int type = 0;

	type = pci_direct_probe();
#endif
	
	if (!(pci_probe & PCI_PROBE_NOEARLY))
		pci_mmcfg_early_init();

	if (x86_init.pci.arch_init && !x86_init.pci.arch_init())
		return 0;

#ifdef CONFIG_PCI_BIOS
	pci_pcbios_init();
#endif
	/*
	 * don't check for raw_pci_ops here because we want pcbios as last
	 * fallback, yet it's needed to run first to set pcibios_last_bus
	 * in case legacy PCI probing is used. otherwise detecting peer busses
	 * fails.
	 */
#ifdef CONFIG_PCI_DIRECT
	pci_direct_init(type);
#endif
	if (!raw_pci_ops && !raw_pci_ext_ops)
		printk(KERN_ERR
		"PCI: Fatal: No config space access function found\n");
	printk("FIXING UP %s %s REV 10\n", __DATE__ ,__TIME__);
	scan_bus();
	printk("FIXUP END\n");
	dmi_check_pciprobe();

	dmi_check_skip_isa_align();

	return 0;
}
arch_initcall(pci_arch_init);
