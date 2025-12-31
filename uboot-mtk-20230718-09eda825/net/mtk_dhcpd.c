// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025
 *
 * Author: Yuzhii
 *
 * Minimal DHCPv4 server for MediaTek web failsafe.
 *
 * Goals:
 * - Provide IP/netmask/gateway/DNS to a directly connected PC
 * - Auto-start with web failsafe (httpd)
 * - Small and self-contained
 */

#include <common.h>
#include <net.h>

#include <net/mtk_dhcpd.h>

#define DHCPD_SERVER_PORT	67
#define DHCPD_CLIENT_PORT	68

/*
 * BOOTP/DHCP has a historical minimum message size of 300 bytes.
 * Some clients (notably Windows) may ignore shorter replies.
 */
#define DHCPD_MIN_BOOTP_LEN	300

/* BOOTP/DHCP message header (RFC 2131) */
struct dhcpd_pkt {
	u8 op;
	u8 htype;
	u8 hlen;
	u8 hops;
	u32 xid;
	u16 secs;
	u16 flags;
	u32 ciaddr;
	u32 yiaddr;
	u32 siaddr;
	u32 giaddr;
	u8 chaddr[16];
	u8 sname[64];
	u8 file[128];
	u8 vend[312];
} __packed;

#define BOOTREQUEST		1
#define BOOTREPLY		2

#define HTYPE_ETHER		1
#define HLEN_ETHER		6

#define DHCPDISCOVER		1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPACK			5

#define DHCP_OPTION_PAD		0
#define DHCP_OPTION_SUBNET_MASK	1
#define DHCP_OPTION_ROUTER	3
#define DHCP_OPTION_DNS_SERVER	6
#define DHCP_OPTION_REQ_IPADDR	50
#define DHCP_OPTION_LEASE_TIME	51
#define DHCP_OPTION_MSG_TYPE	53
#define DHCP_OPTION_SERVER_ID	54
#define DHCP_OPTION_END		255

#define DHCP_FLAG_BROADCAST	0x8000

#define DHCPD_POOL_START_STR	"192.168.1.100"
#define DHCPD_POOL_END_STR	"192.168.1.200"

#define DHCPD_DEFAULT_IP_STR	"192.168.1.1"
#define DHCPD_DEFAULT_NETMASK_STR "255.255.255.0"

#define DHCPD_MAX_CLIENTS	8

static const u8 dhcp_magic_cookie[4] = { 99, 130, 83, 99 };

struct dhcpd_lease {
	bool used;
	u8 mac[6];
	struct in_addr ip;
};

static struct dhcpd_lease leases[DHCPD_MAX_CLIENTS];
static u32 next_ip_host;

static rxhand_f *prev_udp_handler;
static bool dhcpd_running;

static struct in_addr dhcpd_get_server_ip(void)
{
	if (net_ip.s_addr)
		return net_ip;

	return string_to_ip(DHCPD_DEFAULT_IP_STR);
}

static struct in_addr dhcpd_get_netmask(void)
{
	if (net_netmask.s_addr)
		return net_netmask;

	return string_to_ip(DHCPD_DEFAULT_NETMASK_STR);
}

static struct in_addr dhcpd_get_gateway(void)
{
	if (net_gateway.s_addr)
		return net_gateway;

	return dhcpd_get_server_ip();
}

static struct in_addr dhcpd_get_dns(void)
{
	if (net_dns_server.s_addr)
		return net_dns_server;

	return dhcpd_get_server_ip();
}

static bool dhcpd_mac_equal(const u8 *a, const u8 *b)
{
	return memcmp(a, b, 6) == 0;
}

static struct dhcpd_lease *dhcpd_find_lease(const u8 *mac)
{
	int i;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		if (leases[i].used && dhcpd_mac_equal(leases[i].mac, mac))
			return &leases[i];
	}

	return NULL;
}

static bool dhcpd_ip_in_pool(u32 ip_host)
{
	u32 start = ntohl(string_to_ip(DHCPD_POOL_START_STR).s_addr);
	u32 end = ntohl(string_to_ip(DHCPD_POOL_END_STR).s_addr);

	return ip_host >= start && ip_host <= end;
}

static struct in_addr dhcpd_alloc_ip(const u8 *mac)
{
	struct dhcpd_lease *l;
	u32 start, end;
	int i;

	l = dhcpd_find_lease(mac);
	if (l)
		return l->ip;

	start = ntohl(string_to_ip(DHCPD_POOL_START_STR).s_addr);
	end = ntohl(string_to_ip(DHCPD_POOL_END_STR).s_addr);

	if (!next_ip_host)
		next_ip_host = start;

	for (i = 0; i < DHCPD_MAX_CLIENTS; i++) {
		int idx;

		idx = i;
		if (!leases[idx].used) {
			leases[idx].used = true;
			memcpy(leases[idx].mac, mac, 6);
			leases[idx].ip.s_addr = htonl(next_ip_host);

			next_ip_host++;
			if (next_ip_host > end)
				next_ip_host = start;

			return leases[idx].ip;
		}
	}

	/* No free slot: just return the first address in pool */
	{
		struct in_addr ip;
		ip.s_addr = htonl(start);
		return ip;
	}
}

static u8 dhcpd_parse_msg_type(const struct dhcpd_pkt *bp, unsigned int len)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *opt;
	unsigned int optlen;

	if (len < fixed + 4)
		return 0;

	opt = (const u8 *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return 0;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		u8 code;
		u8 olen;

		code = *opt++;
		optlen--;

		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == DHCP_OPTION_MSG_TYPE) {
			if (olen >= 1)
				return opt[0];
		}

		opt += olen;
		optlen -= olen;
	}

	return 0;
}

static bool dhcpd_parse_req_ip(const struct dhcpd_pkt *bp, unsigned int len,
			      struct in_addr *req_ip)
{
	unsigned int fixed = offsetof(struct dhcpd_pkt, vend);
	const u8 *opt;
	unsigned int optlen;

	if (len < fixed + 4)
		return false;

	opt = (const u8 *)bp->vend;
	optlen = len - fixed;

	if (memcmp(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie)))
		return false;

	opt += 4;
	optlen -= 4;

	while (optlen) {
		u8 code;
		u8 olen;

		code = *opt++;
		optlen--;

		if (code == DHCP_OPTION_PAD)
			continue;
		if (code == DHCP_OPTION_END)
			break;

		if (!optlen)
			break;
		olen = *opt++;
		optlen--;

		if (olen > optlen)
			break;

		if (code == DHCP_OPTION_REQ_IPADDR && olen == 4) {
			memcpy(&req_ip->s_addr, opt, 4);
			return true;
		}

		opt += olen;
		optlen -= olen;
	}

	return false;
}

static u8 *dhcpd_opt_add_u8(u8 *p, u8 code, u8 val)
{
	*p++ = code;
	*p++ = 1;
	*p++ = val;
	return p;
}

static u8 *dhcpd_opt_add_u32(u8 *p, u8 code, __be32 val)
{
	*p++ = code;
	*p++ = 4;
	memcpy(p, &val, 4);
	return p + 4;
}

static u8 *dhcpd_opt_add_inaddr(u8 *p, u8 code, struct in_addr addr)
{
	return dhcpd_opt_add_u32(p, code, addr.s_addr);
}

static int dhcpd_send_reply(const struct dhcpd_pkt *req, unsigned int req_len,
			    u8 dhcp_msg_type, struct in_addr yiaddr)
{
	struct dhcpd_pkt *bp;
	struct in_addr server_ip, netmask, gw, dns;
	struct in_addr bcast;
	uchar *pkt;
	uchar *payload;
	int eth_hdr_size;
	u8 *opt;
	int payload_len;
	__be32 lease;

	(void)req_len;

	server_ip = dhcpd_get_server_ip();
	netmask = dhcpd_get_netmask();
	gw = dhcpd_get_gateway();
	dns = dhcpd_get_dns();

	bcast.s_addr = 0xFFFFFFFF;

	pkt = net_tx_packet;
	eth_hdr_size = net_set_ether(pkt, net_bcast_ethaddr, PROT_IP);
	net_set_udp_header(pkt + eth_hdr_size, bcast,
			   DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, 0);

	payload = pkt + eth_hdr_size + IP_UDP_HDR_SIZE;
	bp = (struct dhcpd_pkt *)payload;
	memset(bp, 0, sizeof(*bp));

	bp->op = BOOTREPLY;
	bp->htype = HTYPE_ETHER;
	bp->hlen = HLEN_ETHER;
	bp->hops = 0;
	bp->xid = req->xid;
	bp->secs = req->secs;
	bp->flags = htons(DHCP_FLAG_BROADCAST);
	bp->ciaddr = 0;
	bp->yiaddr = yiaddr.s_addr;
	bp->siaddr = server_ip.s_addr;
	bp->giaddr = 0;
	memcpy(bp->chaddr, req->chaddr, sizeof(bp->chaddr));

	opt = (u8 *)bp->vend;
	memcpy(opt, dhcp_magic_cookie, sizeof(dhcp_magic_cookie));
	opt += 4;

	opt = dhcpd_opt_add_u8(opt, DHCP_OPTION_MSG_TYPE, dhcp_msg_type);
	opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_SERVER_ID, server_ip);
	opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_SUBNET_MASK, netmask);
	opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_ROUTER, gw);
	opt = dhcpd_opt_add_inaddr(opt, DHCP_OPTION_DNS_SERVER, dns);

	lease = htonl(3600);
	opt = dhcpd_opt_add_u32(opt, DHCP_OPTION_LEASE_TIME, lease);

	*opt++ = DHCP_OPTION_END;

	payload_len = (int)((uintptr_t)opt - (uintptr_t)payload);
	if (payload_len < DHCPD_MIN_BOOTP_LEN)
		payload_len = DHCPD_MIN_BOOTP_LEN;

	/* Update UDP header with actual payload length */
	net_set_udp_header(pkt + eth_hdr_size, bcast,
			   DHCPD_CLIENT_PORT, DHCPD_SERVER_PORT, payload_len);

	net_send_packet(pkt, eth_hdr_size + IP_UDP_HDR_SIZE + payload_len);

	return 0;
}

static void dhcpd_handle_packet(uchar *pkt, unsigned int dport,
			       struct in_addr sip, unsigned int sport,
			       unsigned int len)
{
	const struct dhcpd_pkt *bp = (const struct dhcpd_pkt *)pkt;
	u8 msg_type;
	struct in_addr yiaddr;
	struct in_addr req_ip;

	(void)sip;

	if (!dhcpd_running)
		return;

	if (dport != DHCPD_SERVER_PORT || sport != DHCPD_CLIENT_PORT)
		return;

	if (len < offsetof(struct dhcpd_pkt, vend))
		return;

	if (bp->op != BOOTREQUEST)
		return;

	if (bp->htype != HTYPE_ETHER || bp->hlen != HLEN_ETHER)
		return;

	msg_type = dhcpd_parse_msg_type(bp, len);
	if (!msg_type)
		return;

	debug_cond(DEBUG_DEV_PKT, "dhcpd: msg=%u from %pM\n", msg_type, bp->chaddr);

	switch (msg_type) {
	case DHCPDISCOVER:
		yiaddr = dhcpd_alloc_ip(bp->chaddr);
		debug_cond(DEBUG_DEV_PKT, "dhcpd: offer %pI4\n", &yiaddr);
		dhcpd_send_reply(bp, len, DHCPOFFER, yiaddr);
		break;
	case DHCPREQUEST:
		/* If client requests a specific IP, validate it */
		if (dhcpd_parse_req_ip(bp, len, &req_ip)) {
			u32 ip_host = ntohl(req_ip.s_addr);
			if (dhcpd_ip_in_pool(ip_host)) {
				yiaddr = req_ip;
			} else {
				yiaddr = dhcpd_alloc_ip(bp->chaddr);
			}
		} else {
			yiaddr = dhcpd_alloc_ip(bp->chaddr);
		}
		dhcpd_send_reply(bp, len, DHCPACK, yiaddr);
		break;
	default:
		break;
	}
}

static void dhcpd_udp_handler(uchar *pkt, unsigned int dport,
			     struct in_addr sip, unsigned int sport,
			     unsigned int len)
{
	dhcpd_handle_packet(pkt, dport, sip, sport, len);

	if (prev_udp_handler)
		prev_udp_handler(pkt, dport, sip, sport, len);
}

int mtk_dhcpd_start(void)
{
	struct in_addr pool_start;

	/*
	 * Be robust against net_init()/net_clear_handlers() resetting handlers.
	 * If we're already running but the UDP handler is no longer ours, re-hook.
	 */
	if (dhcpd_running) {
		rxhand_f *cur = net_get_udp_handler();

		if (cur != dhcpd_udp_handler) {
			prev_udp_handler = cur;
			net_set_udp_handler(dhcpd_udp_handler);
		}
		return 0;
	}

	/* Ensure we have a usable local IP, otherwise UDP replies will use 0.0.0.0 */
	if (!net_ip.s_addr)
		net_ip = string_to_ip(DHCPD_DEFAULT_IP_STR);
	if (!net_netmask.s_addr)
		net_netmask = string_to_ip(DHCPD_DEFAULT_NETMASK_STR);
	if (!net_gateway.s_addr)
		net_gateway = net_ip;
	if (!net_dns_server.s_addr)
		net_dns_server = net_ip;

	memset(leases, 0, sizeof(leases));

	pool_start = string_to_ip(DHCPD_POOL_START_STR);
	next_ip_host = ntohl(pool_start.s_addr);

	prev_udp_handler = net_get_udp_handler();
	net_set_udp_handler(dhcpd_udp_handler);

	dhcpd_running = true;

	return 0;
}

void mtk_dhcpd_stop(void)
{
	if (!dhcpd_running)
		return;

	/*
	 * If the network loop already cleared handlers, don't resurrect another
	 * handler here. We only restore the previous handler if we are still
	 * installed.
	 */
	if (net_get_udp_handler() == dhcpd_udp_handler)
		net_set_udp_handler(prev_udp_handler);
	prev_udp_handler = NULL;
	dhcpd_running = false;
}
