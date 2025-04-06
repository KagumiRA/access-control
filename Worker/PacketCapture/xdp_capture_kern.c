#include "vmlinux.h"
//#include <linux/bpf.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "xdp_test.h"

#define ETH_P_IP 0x0800
#define ETH_HLEN 14

struct xdp_event {
	__u32 saddr;
	__u32 daddr;
	__u16 sport;
	__u16 dport;
	char payload[512];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 512 * 1024);

} xdp_map SEC(".maps");

// 存储所有服务IP的信息
// key u32 服务IP
// value u32 随意
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, u32);
    __uint(max_entries, 512);
} serviceIp_map SEC(".maps");

SEC("xdp_test")
int xdp_test_prog(struct xdp_md *ctx) {

	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	
	struct ethhdr *eth = data;
	
	if (data + sizeof(struct ethhdr) > data_end) return XDP_PASS;

	if (__bpf_ntohs(eth->h_proto) == ETH_P_IP) {
		struct iphdr *ip = data + sizeof(struct ethhdr);

		// 读ip头长
		__u8 ip_hlen = 0;
		bpf_probe_read_kernel(&ip_hlen, sizeof(ip_hlen), ip);
		// bytes
		ip_hlen = (ip_hlen & 0x0F) << 2;
		__u16 tcp_offset = sizeof(struct ethhdr) + ip_hlen;
		if (data + tcp_offset > data_end) return XDP_PASS;

		__u8 tcp_proto = BPF_CORE_READ(ip, protocol);
		if (tcp_proto == IPPROTO_TCP) {
			struct tcphdr *tcp = data + tcp_offset;
			// 读tcp头长
			void* tcp_loff = data + tcp_offset + 12;

			__u8 tcp_hlen = 0;
			bpf_probe_read_kernel(&tcp_hlen, sizeof(tcp_hlen), tcp_loff);
			tcp_hlen = (tcp_hlen & 0xF0) >> 2;
			//bpf_printk("len : %d", tcp_hlen);
			__u16 payload_offset = tcp_offset + tcp_hlen;
			if (data + payload_offset > data_end) return XDP_PASS;

			// ok tcp头长要读tcp长字段，现在先加struct tcphdr
			void* payload = data + payload_offset;
			
			int sourceIp = BPF_CORE_READ(ip, saddr);
			int *serviceip_value;
			serviceip_value = bpf_map_lookup_elem(&serviceIp_map, &sourceIp);
			// 只监测有关服务的数据
			if (serviceip_value != NULL) {
				//bpf_printk("src YES %d", *serviceip_value);
			} else {
				return XDP_PASS;
			}
			int destIp = BPF_CORE_READ(ip, daddr);
			serviceip_value = bpf_map_lookup_elem(&serviceIp_map, &destIp);
			// 只监测有关服务的数据
			if (serviceip_value != NULL) {
				//bpf_printk("dest YES %d", *serviceip_value);
			} else {
				return XDP_PASS;
			}

			// 唔知點解將ringbuf放在查詢IP前會報unreleased reference
			struct xdp_event *event;	
			event = bpf_ringbuf_reserve(&xdp_map, sizeof(*event), 0);
			if (!event) return XDP_PASS;
			
			event->saddr = BPF_CORE_READ(ip, saddr);
			event->daddr = BPF_CORE_READ(ip, daddr);
			event->sport = BPF_CORE_READ(tcp, source);
			event->dport = BPF_CORE_READ(tcp, dest);
			//long e = bpf_probe_read_kernel(event->payload, 4096, payload);
			bpf_probe_read_kernel(event->payload, 512, payload);
			// 打印payload
			// if (event->payload[0] == 'G') {
			// 	bpf_printk("%s", event->payload);
			// }
			bpf_printk("%s", event->payload);
			bpf_ringbuf_submit(event, 0);
		}
	}

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
