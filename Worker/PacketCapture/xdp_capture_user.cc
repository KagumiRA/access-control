#ifndef XDP_CLIENT_H
#define XDP_CLIENT_H

#include <fstream>
#include <thread>
#include <string.h>

#include "xdp_header.cc"

#define FILENAME "data_client"

struct xdp_event {
	__u32 saddr;
	__u32 daddr;
	__u16 sport;
	__u16 dport;
	char payload[256];
};

struct ring_buffer *xdp_ring_buf = NULL;
// 本来要写到文件上再发送给controller 现在直接内存发
//std::fstream file;


int sub_str(char str[], char pattern[]) {
	int len = strlen(str);
	for (int i = 0; i < len; i++) {
		if (str[i] == pattern[0] && str[i+1] == pattern[1]) {
			return i;
		}
	}
	return -1;
}

uint16_t bigEndianToLittleEndian16(uint16_t value) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
}

static int xdp_event_handler(std::vector<std::unordered_set<std::string>> *dataDistinctSet, void *data, size_t size) {
	struct xdp_event *event = data;

	struct in_addr daddr_s, saddr_s;
	saddr_s.s_addr = event->saddr;
	daddr_s.s_addr = event->daddr;

	// std::cout << "RING BUF HANDLER: " << event->payload << std::endl;
	std::string test_payload;
	std::string test_payload2;
	std::string payload;
	if ((event->payload[0] == 'G' && event->payload[1] == 'E' && event->payload[2] == 'T') || 
		(event->payload[0] == 'P' && event->payload[1] == 'O' && event->payload[2] == 'S' && event->payload[3] == 'T') ||
		(event->payload[0] == 'D' && event->payload[1] == 'E' && event->payload[2] == 'L' && event->payload[3] == 'E' && event->payload[4] == 'T' && event->payload[5] == 'E') ||
		(event->payload[0] == 'P' && event->payload[1] == 'U' && event->payload[2] == 'T') || 
		(event->payload[0] == 'P' && event->payload[1] == 'A' && event->payload[2] == 'T' && event->payload[3] == 'C' && event->payload[4] == 'H') ||
		(event->payload[0] == 'H' && event->payload[1] == 'E' && event->payload[2] == 'A' && event->payload[3] == 'D') 
		)  {
		
		// http url
		char terminal[2];
		terminal[0] = '\r', terminal[1] = '\n';
		int index = sub_str(event->payload, terminal);
		int i = 0;
		for (i = 0; i < index; i++) {
			payload += event->payload[i];
		}
		for (i = 0; i < index; i++) {
			test_payload2 += event->payload[i];
		}
		test_payload2 += "\n";

		payload += " ";
		payload += inet_ntoa(saddr_s);
		payload += " ";
		payload += std::to_string(bigEndianToLittleEndian16(event->sport));
		payload += " ";
		payload += inet_ntoa(daddr_s);
		payload += " ";
		payload += std::to_string(bigEndianToLittleEndian16(event->dport));
		payload += "\n";


		test_payload += event->payload;

	} else if (event->payload[0] == 'H' && event->payload[1] == 'T' && event->payload[2] == 'T' && event->payload[3] == 'P') {
		// HTTP 响应报文  过滤掉

	} else { 
		// TCP 暂时先过滤
		// std::cout << "TCP" << std::endl;
		// std::cout << "SRC: " << event->saddr << " " << event->sport << std::endl;
		// std::cout << "DEST: " << event->daddr << " " << event->dport << std::endl;
		// payload += "TCP";
		// payload += " ";
		// payload += inet_ntoa(saddr_s);
		// payload += " ";
		// payload += std::to_string(bigEndianToLittleEndian16(event->sport));
		// payload += " ";
		// payload += inet_ntoa(daddr_s);
		// payload += " ";
		// payload += std::to_string(bigEndianToLittleEndian16(event->dport));
		// payload += "\n";
		//std::cout << payload;
	}

	// 优化
	// if (auto search = dataDistinctSet->find(payload); search != dataDistinctSet->end()) {
	// 	// std::cout << "Exist same request " << *search << std::endl;
	// }
	// else {
	// 	// std::cout << "New request : " << payload;
	// 	dataDistinctSet->insert(payload);
	// }
	if (payload.length() > 0) {
		if (auto search = (*dataDistinctSet)[0].find(payload); search == (*dataDistinctSet)[0].end()) {
			// std::cout << "New request : " << payload;
			(*dataDistinctSet)[0].insert(payload);
		}
	} 
	if (test_payload.length() > 0) {
		if (auto search = (*dataDistinctSet)[1].find(test_payload); search == (*dataDistinctSet)[1].end()) {
			// std::cout << "New request : " << test_payload;
			(*dataDistinctSet)[1].insert(test_payload);
		}
	} 
	if (test_payload2.length() > 0) {
		if (auto search = (*dataDistinctSet)[2].find(test_payload2); search == (*dataDistinctSet)[2].end()) {
			// std::cout << "New request : " << test_payload;
			(*dataDistinctSet)[2].insert(test_payload2);
		}
	}
	return 0;
}

// 用来构建serviceIp的map 用来只捕捉相关服务的包
void build_serviceIp_map(struct bpf_object *bpf_obj, std::vector<int> &serviceIpIntSet) {
	int serviceIpFd = bpf_object__find_map_fd_by_name(bpf_obj, "serviceIp_map");
	for (int i = 0; i < serviceIpIntSet.size(); i++) {
		int key = serviceIpIntSet[i];
		int value = 1;
		bpf_map_update_elem(serviceIpFd, &key, &value, BPF_ANY);
	}
}

int myxdp(std::vector<char*> ifNameArray, std::vector<std::unordered_set<std::string>> &dataDistinctSet, std::vector<int> &serviceIpIntSet, int *durationTime) {

	signal(SIGINT, close_proc);
	struct xdp_program *prog = NULL;
	prog = xdp_program__open_file("xdp_test_kern.o", "xdp_test", NULL);
	if (!prog) {
		printf("DATA PACKET CAPTURE: Error open xdp program!\n");
		return 0;
	}
	//printf("Success open xdp program!\n");

	int ifindex;
	std::vector<int> ifIndexArray;
	for (int i = 0; i < ifNameArray.size(); i++) { 
		// cni0 会与其它容器网卡冲突
		ifindex = if_nametoindex(ifNameArray[i]);
		//std::cout << "if Name : " << ifNameArray[i] << " " << ifindex << std::endl;
		if (strcmp(ifNameArray[i], "cni0") == 0 || strcmp(ifNameArray[i], "ens3") == 0) {
			continue;
		}
		
		int currentSize = ifIndexArray.size();
		int isExistSameIndex = false;
		for (int j = 0; j < currentSize; j++) {
			if (ifIndexArray[j] == ifindex) {
				isExistSameIndex = true;
				//std::cout << "EXIST SAME : " << ifNameArray[i] << " " << ifindex << std::endl;
			}
		}
		if (isExistSameIndex == false) {
			ifIndexArray.push_back(ifindex);
			//std::cout << "INSERT if Name : " << ifNameArray[i] << " " << ifindex << std::endl;
			
			int ret = xdp_program__attach(prog, ifindex, XDP_MODE_SKB, 0);
			if (ret) {
				printf("DATA PACKET CAPTURE: Error attach to index:%d %s\n", ifindex, ifNameArray[i]);
				close_proc(ifIndexArray, prog);
				return 0;
			}
			printf("DATA PACKET CAPTURE: Success attach to index:%d %s\n", ifindex, ifNameArray[i]);
		}
	}

	int map_fd;
	struct bpf_object *bpf_obj;

	bpf_obj = xdp_program__bpf_obj(prog);
	map_fd = bpf_object__find_map_fd_by_name(bpf_obj, "xdp_map");
	if (map_fd < 0) {
		printf("DATA PACKET CAPTURE: Error get map fd from bpf obj\n");
		close_proc(ifIndexArray, prog);
		return 0;
	}
	xdp_ring_buf = ring_buffer__new(map_fd, xdp_event_handler, (void*)&dataDistinctSet, NULL);

	build_serviceIp_map(bpf_obj, serviceIpIntSet);
	
	// 以后要用
	//file.open("data_client.txt");
	auto start = std::chrono::steady_clock::now();
	//char* filename = makeFile(ifindex);
	// 確定没有文件再创建文件？
	//file.open(FILENAME, std::ios::binary | std::ios::out | std::ios::app);
	//file.open(FILENAME, std::ios::binary | std::ios::out);
	//if (file.is_open()) {
	while(1) {
		auto end = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
		if (duration > *durationTime) {
			break;
		}
		
		// 100 timeout
		ring_buffer__poll(xdp_ring_buf, 100);
	}
	//}
	ring_buffer__free(xdp_ring_buf);
	close_proc(ifIndexArray, prog);
	return 0;
}

#endif
