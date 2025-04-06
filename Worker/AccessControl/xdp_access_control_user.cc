#ifndef XDP_RTREE_USER
#define XDP_RTREE_USER

#include "xdp_header.cc"
#include <iostream>
#include <sstream>

void split_ip(std::vector<std::string> &tokens, std::string name, char terminal) {
    std::string token;
    for (int i = 0; i < name.length(); i++) {
        if (name[i] == terminal) {
            tokens.push_back(token);
            token.clear();
            continue;
        }
        token += name[i];
    }
	tokens.push_back(token);
}

int update_bpf_policy(struct bpf_object *bpf_obj, std::vector<std::vector<std::string>> &policyArray, std::vector<std::vector<int>> &serviceIp,
					std::vector<std::unordered_map<std::string, std::string>> &urlWordDictArray, std::unordered_map<std::string, std::string> &globalSrcIpDict,
					int currentPolicySize, int analyzed_count) {
	// service IP 转换成对应规则表的索引
	int serviceIpIndexFd = bpf_object__find_map_fd_by_name(bpf_obj, "serviceIp_index_map");
	// 规则表 每个规则的前缀需要加索引 例如规则1:2:3:4 索引是3 则3:1:2:3:4
	int policyFd = bpf_object__find_map_fd_by_name(bpf_obj, "policy_map");
	int wordDictFd = bpf_object__find_map_fd_by_name(bpf_obj, "word_dict");
	int ipWordDictFd = bpf_object__find_map_fd_by_name(bpf_obj, "ip_word_dict");

	if (serviceIpIndexFd < 0 || policyFd < 0 || wordDictFd < 0) {
		printf("ACCESS CONTROL : Error get map fd from bpf obj\n");
		return -1;
	}
	// 处理服务IP对应哪些规则
	for (int i = currentPolicySize; i < analyzed_count; i++) {
		// i对应第i个规则表 构造key
		char policyIndex[8] = {'\0'};
		sprintf(policyIndex, "%d", i);
		std::vector<int> ipVec = serviceIp[i];
		for (auto iter = ipVec.begin(); iter != ipVec.end(); iter++) {
			int ip = *iter;
			// 目的IP有多个 所以所有IP都对应一个key值
			bpf_map_update_elem(serviceIpIndexFd, &ip, policyIndex, BPF_ANY);
		}
		
		// 前缀
		char policy[64] = {'\0'};
		for (int j = 0; j < 8; j++) {
			policy[j] = policyIndex[j];
		}
		// 处理规则表
		std::vector<std::string> policyVec = policyArray[i];
		std::cout << "XDP_ACCESS_CONTROL_USER | policy str:";
		for (auto iter = policyVec.begin(); iter != policyVec.end(); iter++) {
			std::cout << *iter << " ";
		}
		std::cout << std::endl;
		for (int j = 0; j < policyVec.size(); j++) {
			
			//std::cout << "POLICY LENGTH : " << policyArray[i][j].length() << std::endl;
			for (int k = 8; k < 64; k++) {
				policy[k] = '\0';
			}
			// 有机会溢出
			for (int k = 8; k < (policyVec[j].length() + 8) && k < 64; k++) {
				policy[k] = policyVec[j][k-8];
			}
			int n = 1;
			bpf_map_update_elem(policyFd, policy, &n, BPF_ANY);
			std::cout << "XDP_ACCESS_CONTROL_USER | policy :";
			for (int k = 0; k < 64; k++) {
				if (policy[k] == '\0') {
					std::cout << "~";
				} else {
					std::cout << policy[k];
				}
			}
			std::cout << std::endl;
		}


		// std::cout << "XDP_ACCESS_CONTROL_USER - ip word dict : ";
		// for (auto iter = globalSrcIpDict.begin(); iter != globalSrcIpDict.end(); iter++) {
		// 	std::cout << iter->first << " " << iter->second << " ";
		// }
		std::cout << std::endl;
		for (auto iter = globalSrcIpDict.begin(); iter != globalSrcIpDict.end(); iter++) {
			unsigned int ip;
    		std::stringstream ss(iter->first); // 创建一个字符串流
    		ss >> ip;
			char policyEntry[4] = {'\0'};
			for (int j = 0; j < iter->second.length(); j++) {
				policyEntry[j] = iter->second[j];
			}
			bpf_map_update_elem(ipWordDictFd, &ip, policyEntry, BPF_ANY);
			std::cout << "XDP_ACCESS_CONTROL_USER | ip word dict :";
			std::cout << ip << " " << policyEntry << std::endl;

		}

		// 该规则表的url字典
		std::unordered_map<std::string, std::string> urlWordDict = urlWordDictArray[i];
		std::cout << "XDP_ACCESS_CONTROL_USER - url word dict : ";
		for (auto iter = urlWordDict.begin(); iter != urlWordDict.end(); iter++) {
			std::cout << iter->first << " " << iter->second << " ";
		}
		std::cout << std::endl;
		for (auto iter = urlWordDict.begin(); iter != urlWordDict.end(); iter++) {
			
			// 等价于http_token
			char http_token[64] = {'\0'};
			char policyEntry[4] = {'\0'};
			// 前缀
			for (int j = 0; j < 8; j++) {
				http_token[j] = policyIndex[j];
			}
			for (int j = 8; j < (iter->first.length() + 8) && j < 64; j++) {
				http_token[j] = iter->first[j-8];
			}
			for (int j = 0; j < iter->second.length(); j++) {
				policyEntry[j] = iter->second[j];
			}
			
			// std::cout << "WORD DICT :";
			// for (int j = 0; j < 64; j++) {
			// 	if (http_token[j] == '\0') {
			// 		std::cout << '~';
			// 	} else {
			// 		std::cout << http_token[j];
			// 	}
			// }
			// std::cout << std::endl;
			
			bpf_map_update_elem(wordDictFd, http_token, policyEntry, BPF_ANY);
		}
		
	}
	
	return 0;
}


int xdp_access_control(std::vector<char*> ifNameArray, std::vector<std::vector<std::string>> &policyArray, 
	std::vector<std::vector<int>> &serviceIp, std::vector<std::unordered_map<std::string, std::string>> &urlWordDictArray,
	std::unordered_map<std::string, std::string> &globalSrcIpDict, int *analyzed_count,
	int *durationTime) {
	struct xdp_program *prog;
	signal(SIGINT, close_proc);
    prog = xdp_program__open_file("xdp_access_control_kern.o", "xdp", NULL);
    if (!prog) {
		printf("ACCESS CONTROL : Error open xdp_access_control program!\n");
		return 0;
	}
	printf("ACCESS CONTROL : Success open xdp access control program!\n");

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
		// 避免attach IPV6接口
		for (int j = 0; j < currentSize; j++) {
			if (ifIndexArray[j] == ifindex) {
				isExistSameIndex = true;
				std::cout << "ACCESS CONTROL : EXIST SAME : " << ifNameArray[i] << " " << ifindex << std::endl;
			}
		}
		if (isExistSameIndex == false) {
			ifIndexArray.push_back(ifindex);
			//std::cout << "ACCESS CONTROL : INSERT if Name : " << ifNameArray[i] << " " << ifindex << std::endl;
			
			int ret = xdp_program__attach(prog, ifindex, XDP_MODE_SKB, 0);
			if (ret) {
				printf("ACCESS CONTROL : Error attach to index:%d %s\n", ifindex, ifNameArray[i]);
				close_proc(ifIndexArray, prog);
				return 0;
			}
			printf("ACCESS CONTROL : Success attach to index:%d %s\n", ifindex, ifNameArray[i]);
		}
		//printf("index : %d %s\n", ifindex, ifNameArray[i]);
	}

	struct bpf_object *bpf_obj;
	bpf_obj = xdp_program__bpf_obj(prog);

	// 找到尾调用的程序的fd
	struct bpf_program* test;
	const char *xdp_program_name = NULL;
	int subProgFd[5];
	bpf_object__for_each_program(test, bpf_obj) {
		xdp_program_name = bpf_program__name(test);
		/*
		if (strcmp(xdp_program_name, "xdp_prog2") == 0) {
			subProgFd[0] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[0]);
		}
		
		if (strcmp(xdp_program_name, "xdp_prog3") == 0) {
			subProgFd[1] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[1]);
		}
		*/
		if (strcmp(xdp_program_name, "xdp_prog_parse_http1") == 0) {
			subProgFd[0] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[0]);
		}
		if (strcmp(xdp_program_name, "xdp_prog_parse_http2") == 0) {
			subProgFd[1] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[1]);
		}
		if (strcmp(xdp_program_name, "xdp_prog_parse_http3") == 0) {
			subProgFd[2] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[2]);
		}
		if (strcmp(xdp_program_name, "xdp_prog_parse_http_end") == 0) {
			subProgFd[3] = bpf_program__fd(test);
			printf("ACCESS CONTROL : Sub xdp prog : %s %d\n", xdp_program_name, subProgFd[3]);
		}		
	}
	// 构建尾调用表
	int jmpMapFd = bpf_object__find_map_fd_by_name(bpf_obj, "tail_jmp_map");
	if (jmpMapFd < 0) {
		printf("ACCESS CONTROL : Error get jmp map!\n");
		close_proc(ifIndexArray, prog);
		return 0;
	}
	for (int i = 0; i < 4; i++) {
		if (bpf_map_update_elem(jmpMapFd, &i, &subProgFd[i], BPF_ANY) < 0){
			printf("ACCESS CONTROL : Failed insert prog to jmp map\n");
			close_proc(ifIndexArray, prog);
			return 0;
		}
	}

	// ----------------------------------------------------------------------
	// 构建规则
	// std::vector<std::string> &serviceIp
	// std::vector<std::vector<std::string>> &policyArray
	// std::vector<std::unordered_map<std::string, std::string>> &wordDictArray

	printf("ACCESS CONTROL : XDP access controlling\n");
	// 存储当前已经处理的策略索引数
	int currentPolicySize = 0;
	auto start = std::chrono::steady_clock::now();
	while (1) {
		if (*analyzed_count > currentPolicySize) {
			// 不传analyzed_count指针是因为其它线程会在update_bpf_policy途中改这个值
			int err = update_bpf_policy(bpf_obj, policyArray, serviceIp, urlWordDictArray, globalSrcIpDict, currentPolicySize, *analyzed_count);
			if (err == -1) {
				close_proc(ifIndexArray, prog);
				return 0;
			}
			currentPolicySize = *analyzed_count;
			//printf("ACCESS CONTROL : Updated policies\n");
		}
		auto end = std::chrono::steady_clock::now();
		sleep(1);
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
		if (duration > *durationTime) {
			break;
		}
	}

	std::cout << "XDP ACCESS CONTROL USER | end" << std::endl;
	close_proc(ifIndexArray, prog);
	return 0;
}

#endif
/*
	// 把wordDict和fingerprint的格式转换为兼容ebpf程序
	// 构造内核字典
	std::unordered_map<std::string, int>::iterator iter;
	
	for (iter = wordDict.begin(); iter != wordDict.end(); iter++) {
		int i = 0;
		char word[64] = {'\0'};
		for (i = 0; i < iter->first.length(); i++) {
			word[i] = iter->first[i];
		}

		// 把字典值int转为char[]存储
		char value[4] = {'\0'};
		std::string valueInt = std::to_string(iter->second);
		char* valueChar = valueInt.c_str();
		for (int i = 0; i < 4; i++) {
			value[i] = valueChar[i];
		}
		std::cout << "wordDict : " << word << " | " << value << std::endl;
		bpf_map_update_elem(wordDictMapFd, word, value, 0);
	}

	// 构造内核ip映射service表
	std::unordered_map<std::string, std::string>::iterator iter2;
	
	for (iter2 = ipServiceDict.begin(); iter2 != ipServiceDict.end(); iter2++) {
		std::cout << "ipServiceDict : " << iter2->first << " | " << iter2->second << std::endl;
		std::vector<std::string> tokens;
		split_ip(tokens, iter2->first, ':');
		int ipInt, portInt;
		ipInt = inet_addr(tokens[0].c_str());
		std::istringstream ss(tokens[1]);
		ss >> portInt;
		
		int key = ipInt ^ (portInt << 16) ^ (portInt << 8);
		char serviceName[64] = {'\0'};
		for (int i = 0; i < iter2->second.length(); i++) {
			serviceName[i] = iter2->second[i];
		}
		
		bpf_map_update_elem(ipServiceDictMapFd, &key, serviceName, 0);
	}
	
	// 未完成
	// 构造内核指纹表 先放到set中去重
	//std::vector<std::string>& pDataFingerprintStr
	std::vector<std::string>::iterator iter4;
	for (int i = 0; i < pDataFpSet.size(); i++) {
		
	}
	char testFingerprint[64] = {'\0'};
	testFingerprint[0] = '0';
	testFingerprint[1] = '0';
	testFingerprint[2] = '1';
	testFingerprint[3] = '2';
	testFingerprint[4] = '6';
	int testValue = 1;
	bpf_map_update_elem(fingerprintDictMapFd, testFingerprint, &testValue, 0);
	*/
	//sleep(10);

/*
	struct bpf_map *outer_map = bpf_object__find_map_by_name(bpf_obj, "policy_outer_map");
	if (outer_map == NULL) {
		printf("ACCESS CONTROL : Error find outer map\n");
		close_proc(ifIndexArray, prog);
		return 0;
	}
	// 为双层map建立内层 dummy map 用来过verifier检测
	int innerMapFd = bpf_map_create(
		BPF_MAP_TYPE_HASH,
		"inner_map", // type
		sizeof(__u32), // key_size
		sizeof(char[64]), // value_size 假设指纹最多为64
		512, // max_entries
		0 // flag
	);
	if (bpf_map__set_inner_map_fd(outer_map, innerMapFd) != 0) {
		printf("ACCESS CONTROL : Error set inner map fd to outer map\n");
		close(innerMapFd);
		close_proc(ifIndexArray, prog);
		return 0;
	}
	// 一定要close 内层map 防止内存泄露 因为kernel会释放内层map内存 user就不用理了
	close(innerMapFd);

	int err = insert_policy2map(bpf_obj, policyArray, serviceIp, wordDictArray);
	if (err == -1) {
		printf("ACCESS CONTROL : Error insert policy to inner map\n");
		close_proc(ifIndexArray, prog);
		return 0;
	}
	*/

/*
int insert_policy2map(struct bpf_object *bpf_obj, std::vector<std::vector<std::string>> &policyArray, 
	std::vector<std::string> &serviceIp, std::vector<std::unordered_map<std::string, std::string>> &wordDictArray) {
	int outer_map_fd = bpf_object__find_map_fd_by_name(bpf_obj, "policy_outer_map");
  	if (outer_map_fd < 0) {
    	return -1;
  	}
	int ret = 0;
  	const __u32 inner_key = 12;
  	const __u32 inner_value = 34;
  	if (bpf_map_update_elem(
        inner_map_fd, &inner_key, &inner_value, 0)) {
    	printf("Failed to insert into inner map!\n");
    	goto err;
  	}
  	const __u32 outer_key = 42;
  	if (bpf_map_update_elem(
        outer_map_fd, &outer_key, &inner_map_fd, 0)) {
    	printf("Failed to insert into outer map!\n");
    	goto err;
  	}
  	goto out;
	err:
  		ret = -1;
	out:
  		close(inner_map_fd); // Important!
	for (int i = 0; i < serviceIp.size(); i++) {
		int inner_map_fd = bpf_map_create(
      		BPF_MAP_TYPE_HASH, // type 
			"inner_map", // name
      		sizeof(__u32), // key_size
      		sizeof(char[64]), // value_size
      		512, // max_entries
      		0); // flag
		if (inner_map_fd < 0) {
			return -1;
		}
		char outer_key[16];
		for (int j = 0; j < serviceIp[i].size(); j++) {

		}
		bpf_map_update_elem(outer_map_fd, &outer_key, &inner_map_fd, 0);
		close(inner_map_fd); // Important!
	}
	// 把wordDict和fingerprint的格式转换为兼容ebpf程序
	// 构造内核字典
	std::unordered_map<std::string, int>::iterator iter;
	
	for (iter = wordDict.begin(); iter != wordDict.end(); iter++) {
		int i = 0;
		char word[64] = {'\0'};
		for (i = 0; i < iter->first.length(); i++) {
			word[i] = iter->first[i];
		}

		// 把字典值int转为char[]存储
		char value[4] = {'\0'};
		std::string valueInt = std::to_string(iter->second);
		char* valueChar = valueInt.c_str();
		for (int i = 0; i < 4; i++) {
			value[i] = valueChar[i];
		}
		std::cout << "wordDict : " << word << " | " << value << std::endl;
		bpf_map_update_elem(wordDictMapFd, word, value, 0);
	}
	
  	return ret;
}
*/
