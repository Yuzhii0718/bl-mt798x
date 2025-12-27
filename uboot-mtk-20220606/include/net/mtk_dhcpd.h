// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025
 *
 * Author: Yuzhii
 *
 * Minimal DHCPv4 server for MediaTek web failsafe.
 */

#ifndef __NET_MTK_DHCPD_H__
#define __NET_MTK_DHCPD_H__

int mtk_dhcpd_start(void);
void mtk_dhcpd_stop(void);

#endif /* __NET_MTK_DHCPD_H__ */
