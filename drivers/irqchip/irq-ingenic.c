/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform IRQ support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/ingenic.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/io.h>

struct ingenic_intc_data {
	void __iomem *base;
	struct irq_domain *domain;
	unsigned num_chips;
};

#define JZ_REG_INTC_STATUS	0x00
#define JZ_REG_INTC_MASK	0x04
#define JZ_REG_INTC_SET_MASK	0x08
#define JZ_REG_INTC_CLEAR_MASK	0x0c
#define JZ_REG_INTC_PENDING	0x10
#define CHIP_SIZE		0x20

static void intc_cascade(struct irq_desc *desc)
{
	struct ingenic_intc_data *intc = irq_desc_get_handler_data(desc);
	struct irq_chip *irq_chip = irq_data_get_irq_chip(&desc->irq_data);
	uint32_t irq_reg;
	unsigned i;

	chained_irq_enter(irq_chip, desc);

	for (i = 0; i < intc->num_chips; i++) {
		struct irq_chip_generic *gc = irq_get_domain_generic_chip(
				intc->domain, i * 32);

		irq_reg = irq_reg_readl(gc, JZ_REG_INTC_PENDING);
		if (!irq_reg)
			continue;

		generic_handle_irq(irq_linear_revmap(intc->domain,
					__fls(irq_reg) + (i * 32)));
	}

	chained_irq_exit(irq_chip, desc);
}

static void intc_irq_set_mask(struct irq_chip_generic *gc, uint32_t mask)
{
	struct irq_chip_regs *regs = &gc->chip_types->regs;

	writel(mask, gc->reg_base + regs->enable);
	writel(~mask, gc->reg_base + regs->disable);
}

void ingenic_intc_irq_suspend(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	intc_irq_set_mask(gc, gc->wake_active);
}

void ingenic_intc_irq_resume(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	intc_irq_set_mask(gc, gc->mask_cache);
}

static int __init ingenic_intc_of_init(struct device_node *node,
				       unsigned num_chips)
{
	struct ingenic_intc_data *intc;
	int parent_irq, err = 0;
	unsigned i;

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc) {
		err = -ENOMEM;
		goto out_err;
	}

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		err = -EINVAL;
		goto out_free;
	}

	err = irq_set_handler_data(parent_irq, intc);
	if (err)
		goto out_unmap_irq;

	intc->num_chips = num_chips;
	intc->base = of_iomap(node, 0);
	if (!intc->base) {
		err = -ENODEV;
		goto out_unmap_irq;
	}

	intc->domain = irq_domain_add_linear(node, num_chips * 32,
				       &irq_generic_chip_ops, NULL);
	if (!intc->domain) {
		err = -ENOMEM;
		goto out_unmap_base;
	}

	err = irq_alloc_domain_generic_chips(intc->domain, 32, 1,
			"INTC", handle_level_irq, 0, IRQ_NOPROBE | IRQ_LEVEL,
			0);
	if (err)
		goto out_domain_remove;

	for (i = 0; i < num_chips; i++) {
		struct irq_chip_generic *gc = irq_get_domain_generic_chip(
				intc->domain, i * 32);
		struct irq_chip_type *ct = gc->chip_types;

		gc->wake_enabled = IRQ_MSK(32);
		gc->reg_base = intc->base + (i * CHIP_SIZE);

		ct->regs.enable = JZ_REG_INTC_CLEAR_MASK;
		ct->regs.disable = JZ_REG_INTC_SET_MASK;
		ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
		ct->chip.irq_mask = irq_gc_mask_disable_reg;
		ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
		ct->chip.irq_set_wake = irq_gc_set_wake;
		ct->chip.irq_suspend = ingenic_intc_irq_suspend;
		ct->chip.irq_resume = ingenic_intc_irq_resume;

		/* Mask all irqs */
		irq_reg_writel(gc, 0xffffffff, JZ_REG_INTC_SET_MASK);
	}

	irq_set_chained_handler_and_data(parent_irq, intc_cascade, intc);
	return 0;

out_domain_remove:
	irq_domain_remove(intc->domain);
out_unmap_base:
	iounmap(intc->base);
out_unmap_irq:
	irq_dispose_mapping(parent_irq);
out_free:
	kfree(intc);
out_err:
	return err;
}

static int __init intc_1chip_of_init(struct device_node *node,
				     struct device_node *parent)
{
	return ingenic_intc_of_init(node, 1);
}
IRQCHIP_DECLARE(jz4740_intc, "ingenic,jz4740-intc", intc_1chip_of_init);

static int __init intc_2chip_of_init(struct device_node *node,
	struct device_node *parent)
{
	return ingenic_intc_of_init(node, 2);
}
IRQCHIP_DECLARE(jz4770_intc, "ingenic,jz4770-intc", intc_2chip_of_init);
IRQCHIP_DECLARE(jz4775_intc, "ingenic,jz4775-intc", intc_2chip_of_init);
IRQCHIP_DECLARE(jz4780_intc, "ingenic,jz4780-intc", intc_2chip_of_init);
