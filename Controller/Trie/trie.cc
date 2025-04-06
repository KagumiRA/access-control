#ifndef TRAIN_CC
#define TRAIN_CC

#include <iostream>
#include <fstream>
#include <ctime>
#include <sstream>
#include <arpa/inet.h>

#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include "bert_client.cc"

#define threshold 0.4

// children用作以后扩充成为真正的树，目前树高度只有3
struct valueNode {
    std::vector<valueNode*> parent;
    std::vector<valueNode*> children;
    std::string token;

    valueNode() : token(), parent(), children() {}
    valueNode(std::string token) : token(token), parent(), children() {}
    virtual ~valueNode() {
        //std::cout << "Deleting node: " << token << std::endl;
    }
};

struct destServiceNode : public valueNode {
    destServiceNode(std::string token) : valueNode(token) {}
};

struct srcServiceNode : public valueNode {
    // 记录要对应哪个父节点（目的节点）的路径
    // 第一维指向对应目的节点 第二维从该目的节点找源节点的请求路径
    std::vector<std::vector<int>> pathIndex; 
    srcServiceNode(std::string token) : valueNode(token), pathIndex() {}  
};

// outcluster front-end GET /catalogue?sort=id&size=3&tags=sport HTTP/1.1

// 应该使用变长参数
void split_url_token(std::vector<std::string>& tokens, std::string httpUrl, std::vector<char> splitCharSet) {
    int urlLength = httpUrl.length();
    std::string token;
    // 可能有错？ 将只有/的sentence当成访问index.html
    if (urlLength == 1 && httpUrl[0] == '/') {
        tokens.push_back("/index.html");
        return;
    }

    // 默认第一个为/ 方便处理
    token += '/';
    for (int i = 1; i < urlLength; i++) {
        for (int j = 0; j < splitCharSet.size(); j++) {
            if (httpUrl[i] == splitCharSet[j]) {
                tokens.push_back(token);
                token.clear();
            }
        }
        token += httpUrl[i];
    }
    if (token.length() > 0) {
        tokens.push_back(token);
    }
    /*
    for (int i = 1; i < urlLength; i++) {
        bool isEqualSplitChar = false;
        for (int j = 0; j < splitCharSet.size(); j++) {
            if (httpUrl[i] == splitCharSet[j]) {
                if (token.length() > 0) {
                    tokens.push_back(token);
                    token.clear();
                }
                isEqualSplitChar = true;
                // 将变量部分标识
                if (httpUrl[i] == '=' || httpUrl[i] == '?' || httpUrl[i] == '&') {
                    token += httpUrl[i];
                }
            }
        }
        if (isEqualSplitChar == false) {
            token += httpUrl[i];
        }
    }
    if (token.length() > 0) {
        tokens.push_back(token);
    }
    */
    return;
}

class parserTree {
    public:
        parserTree() : wordSet(5) {
            this->root = new valueNode;
            this->root->token = "root";
        }

        void test() {
            valueNode *vNode = new valueNode("");
            destServiceNode *dNode = new destServiceNode("");
            srcServiceNode *sNode = new srcServiceNode("");

            if (dynamic_cast<destServiceNode*>(dNode)) {
                std::cout << "The node is a destServiceNode." << std::endl;
            }
        }

        valueNode* insert_API_token(valueNode* node, std::string token) {
            int sameTokenIndex = -1;
            for (int i = 0; i < node->children.size(); i++) {
                if (node->children[i]->token.compare(token) == 0) {
                    sameTokenIndex = i;
                }
            }
            if (sameTokenIndex != -1) {
                node = node->children[sameTokenIndex];
            } else {
                valueNode *tmpNode = new valueNode(token);
                node->children.push_back(tmpNode);
                tmpNode->parent.push_back(node);
                node = node->children.back();
            }
            return node;
        }

        void insert(std::vector<std::string> dataSet, int mode) {
            valueNode *node = this->root;
            destServiceNode *destNode;
            srcServiceNode *srcNode;
            std::unordered_map<std::string, destServiceNode*>::iterator destIter;
            std::unordered_map<std::string, srcServiceNode*>::iterator srcIter;
            std::string sServiceName, dServiceName;
            // TCP
            if (mode == 1) {
                srcIter = srcServiceMap.find(dataSet[1]);
                destIter = destServiceMap.find(dataSet[2]);
                sServiceName = dataSet[1];
                dServiceName = dataSet[2];
            } else if (mode == 2) { 
                // HTTP
                srcIter = srcServiceMap.find(dataSet[3]);
                destIter = destServiceMap.find(dataSet[4]);
                sServiceName = dataSet[3];
                dServiceName = dataSet[4];

                std::vector<std::string> urlTokens;
                std::vector<char> splitCharSet = {'/', '&', '?', '='};
                split_url_token(urlTokens, dataSet[1], splitCharSet);
                // 增加url的终止节点 避免某些url是某个url的子集 导致分不清api 并且可以接源服务名
                // 好像不用加了 直接判断类型是否为目的节点
                //urlTokens.push_back("end");

                // 插入token
                // 插入HTTP URL TOKEN
                for (int i = 0; i < urlTokens.size(); i++) {
                    node = insert_API_token(node, urlTokens[i]);
                }
                // 插入HTTP METHOD
                node = insert_API_token(node, dataSet[0]);
                // 插入HTTP VERSION
                node = insert_API_token(node, dataSet[2]);
            }

            // 去重目的服务
            if (destIter == destServiceMap.end()) {
                destNode = new destServiceNode(dServiceName);
                destServiceMap.emplace(dServiceName, destNode);
            } else {
                destNode = destIter->second;
            }
            // 去重源服务
            if (srcIter == srcServiceMap.end()) {
                // 新节点 询问BERT时用
                newsrcServiceSet.emplace(sServiceName);
                srcNode = new srcServiceNode(sServiceName);
                srcServiceMap.emplace(sServiceName, srcNode);
            } else {
                srcNode = srcIter->second;
            }

            //insert_API(urlTokens, httpMethod, httpVersion, srcNode, destNode);
            // 插入目的服务和源服务
            bool isExistSameService = false;
            // 记录该url在目的服务的哪个父index
            int urlIndex = -1;
            for (int i = 0; i < node->children.size(); i++) {
                if (node->children[i] == destNode) {
                    isExistSameService = true;
                    for (int j = 0; j < destNode->parent.size(); j++) {
                        if (destNode->parent[j] == node) {
                            urlIndex = j;
                            break;
                        }
                    }
                    if (urlIndex == -1) {
                        std::cout << "MAKE POLICY : ERROR INSERT" << std::endl;
                        std::cout << destNode->token << " " << node->token << std::endl;
                    }
                }
            }
            if (isExistSameService == false) {
                node->children.push_back(destNode);
                destNode->parent.push_back(node);
                urlIndex = destNode->parent.size() - 1;
            }

            isExistSameService = false;
            for (int i = 0; i < destNode->children.size(); i++) {
                if (destNode->children[i] == srcNode) {
                    isExistSameService = true;
                }
            }
            if (isExistSameService == false) {
                destNode->children.push_back(srcNode);
            }

            // 找到源节点指向的目的节点的索引
            isExistSameService = false;
            int destIndex = -1;
            for (int i = 0; i < srcNode->parent.size(); i++) {
                if (srcNode->parent[i] == destNode) {
                    isExistSameService = true;
                    destIndex = i;
                }
            }
            if (isExistSameService == false) {
                srcNode->parent.push_back(destNode);
                destIndex = srcNode->parent.size() - 1;
                // 初始化目的节点路径数组
                srcNode->pathIndex.push_back(std::vector<int>());
            }
            //std::cout << "LOOK : " << destNode->token << " " << destNode->parent.size() << std::endl;
            // 如果新版本是同一url直接添加会不会有问题 目前认为新版本的数据都是去重复化 不会有问题
            srcNode->pathIndex[destIndex].push_back(urlIndex);
        }

        void delete_srcService(std::string serviceName) {
            std::unordered_map<std::string, srcServiceNode*>::iterator iter;
            iter = srcServiceMap.find(serviceName);
            if (iter != srcServiceMap.end()) {

                // 目的节点要删除对应源节点
                srcServiceNode *srcNode = iter->second;
                // 如果要删除目的服务节点的对应url路径 需要遍历其连接的所有源服务节点查看有没有引用该路径 目前先保留 因为即使没有源节点引用 也只是存着路径，本来该目的节点就会开放该API
                /*
                for (int i = 0; i < srcNode->pathIndex.size(); i++) {
                    std::vector<int> path = srcNode->pathIndex[i];
                    destServiceNode *destNode = srcNode->parent[i];
                    for (int j = 0; j < path.size(); j++) {
                        delete_node_iter(destNode->parent[path[j]], destNode);
                    }
                }
                */
                for (int i = 0; i < srcNode->parent.size(); i++) {
                    destServiceNode *destNode = srcNode->parent[i];
                    for (int j = 0; j < destNode->children.size(); j++) {
                        if (destNode->children[j] == srcNode) {
                            destNode->children.erase(destNode->children.begin() + j);
                        }
                    }
                }
                delete srcNode;
                srcServiceMap.erase(serviceName);
            } else {
                std::cout << "没有该源节点 删除错误！" << std::endl;
            }
        }

        void delete_node_iter(valueNode *node, valueNode *childNode) {

            // 删除该节点指向的子节点
            for (int i = 0; i < node->children.size(); i++) {
                if (node->children[i] == childNode) {
                    node->children.erase(node->children.begin() + i);
                    break;
                }
            }

            if (node == this->root) {
                return;
            }
            //std::cout << "HI : " << node->token << " " << childNode->token << std::endl;
            // 当该节点的孩子为0 那么有机会上层节点也要删除 因为有可能没有其它URL路径使用该节点
            if (node->children.size() == 0) {
                delete_node_iter(node->parent[0], node);
                delete node;
            }
        }

        void delete_destService(std::string serviceName) {
            std::unordered_map<std::string, destServiceNode*>::iterator iter;
            iter = destServiceMap.find(serviceName);
            if (iter != destServiceMap.end()) {
                destServiceNode *destNode = iter->second;
                // 删除所有指向该目的节点的源节点指向的路径
                for (int i = 0; i < destNode->children.size(); i++) {
                    srcServiceNode *srcNode = destNode->children[i];
                    for (int j = 0; j < srcNode->parent.size(); j++) {
                        if (srcNode->parent[j] == destNode) {
                            srcNode->parent.erase(srcNode->parent.begin() + j);
                            srcNode->pathIndex[j].clear();
                            srcNode->pathIndex.erase(srcNode->pathIndex.begin() + j);
                            break;
                        }
                    }
                }
                // 删除所有该目的节点指向的路径
                for (int i = 0; i < destNode->parent.size(); i++) {
                    delete_node_iter(destNode->parent[i], destNode);
                }
                
                
            } else {
                std::cout << "没有该目的节点 删除错误！" << std::endl;
            }
        }

        void delete_service(std::string serviceName) {
            delete_srcService(serviceName);
            delete_destService(serviceName);
        }

        void traverse_tree_iter(valueNode *node, int &count) {
            if (dynamic_cast<srcServiceNode*>(node)) {
                return;
            }
            count++;
            for (int i = 0; i < node->children.size(); i++) {
                traverse_tree_iter(node->children[i], count);
            }
        }

        // 用来统计目前树中有多少节点 不完全
        int traverse_tree() {
            valueNode *node = this->root;
            int count = 0;
            for (int i = 0; i < node->children.size(); i++) {
                traverse_tree_iter(node->children[i], count);
            }
            // std::cout << "MAKE POLICY : TREE HAS " << count << " NODES" << std::endl;
            return count;
        }

        void stat_token_iter(valueNode *node, int &count) {
            if (dynamic_cast<destServiceNode*>(node)) {
                return;
            }
            count++;
            for (int i = 0; i < node->children.size(); i++) {
                stat_token_iter(node->children[i], count);
            }
        }

        // 统计树中token个数
        int stat_token() {
            valueNode *node = this->root;
            int count = 0;
            for (int i = 0; i < node->children.size(); i++) {
                stat_token_iter(node->children[i], count);
            }
            // std::cout << "MAKE POLICY : TREE HAS " << count << " NODES" << std::endl;
            count += this->destServiceMap.size();
            count += this->srcServiceMap.size();
            return count;
        }
        
        /*
        void traverse_print_token(std::vector<std::string> &tokenList, int mode, int &count) {
                count++;
                // TCP 有源服务
                if (tokenList.size() <= 2 && mode == 1) {
                    std::cout << "TCP - ";
                    std::cout << "srcService : " << tokenList[1] << " ";
                    std::cout << "destService : " << tokenList[0] << std::endl;
                } else if (tokenList.size() <= 2 && mode == 2) {
                    // 没有源服务
                    std::cout << "TCP - ";
                    std::cout << "destService : " << tokenList[0] << std::endl;
                } else {
                    std::cout << "HTTP - ";
                    // 有源服务
                    if (mode == 1) {
                        std::cout << "SOURCE : " << tokenList[tokenList.size() - 1] << " ";
                        std::cout << "DEST : " << tokenList[tokenList.size() - 2] << " ";
                        std::cout << "URL : ";
                        for (int i = 0; i < tokenList.size() - 4; i++) {
                            std::cout << tokenList[i];
                        }                
                        std::cout << " ";
                        std::cout << "METHOD : " << tokenList[tokenList.size() - 4] << " ";
                        std::cout << "VERSION : " << tokenList[tokenList.size() - 3] << std::endl;
                    } else if (mode == 2) {
                        // 源服务被删后只剩目的服务
                        std::cout << "SOURCE : UNKNOWN ";
                        std::cout << "DEST : " << tokenList[tokenList.size() - 1] << " ";
                        std::cout << "URL : ";
                        for (int i = 0; i < tokenList.size() - 3; i++) {
                            std::cout << tokenList[i];
                        }
                        std::cout << " ";
                        std::cout << "METHOD : " << tokenList[tokenList.size() - 3] << " ";
                        std::cout << "VERSION : " << tokenList[tokenList.size() - 2] << std::endl;
                    }
                }
        }

        void traverse_node(std::vector<std::string> &tokenList, valueNode *node, int &count) {
            tokenList.push_back(node->token);


            if (node->children.size() == 0) {
                // 查看当前叶节点是目的节点还是源节点
                // 有源节点
                if (dynamic_cast<srcServiceNode*>(node)) {
                    traverse_print_token(tokenList, 1, count);
                // 只有目的节点
                } else if (dynamic_cast<destServiceNode*>(node)){
                    traverse_print_token(tokenList, 2, count);
                }

            } else {
                for (int i = 0; i < node->children.size(); i++) {
                    traverse_node(tokenList, node->children[i], count);
                }
            }            
            tokenList.pop_back();
            //tokenPtrList.pop_back();
        }

        void traverse_tree() {
            std::vector<std::string> tokenList;
            int count = 0;
            //std::vector<std::string> tokenPtrList;
            for (int i = 0; i < this->root->children.size(); i++) {
                //traverse_node(tokenList, this->root->children[i], count, tokenPtrList);
                traverse_node(tokenList, this->root->children[i], count);
            }
            std::cout << "COUNT : " << count << std::endl;
        }
        */

        void traverse_srcNode_iter(std::vector<std::string> &tokenList, valueNode *node, int *count) {
            if (node == this->root) {
                // TCP
                if (tokenList.size() == 2) {
                    std::cout << "TCP - srcService:" << tokenList[0] << " destService:" << tokenList[1] << std::endl;
                } else {
                    // HTTP
                    std::cout << "HTTP - srcService:" << tokenList[0] << " destService:" << tokenList[1] << " ";
                    std::cout << "HTTP METHOD:" << tokenList[3] << " ";
                    std::cout << "HTTP URL:";
                    for (int i = tokenList.size() - 1; i > 3; i--) {
                        std::cout << tokenList[i];
                    }
                    std::cout << " HTTP VERSION:" << tokenList[2] << std::endl;
                }
                (*count)++;
                return;
            }
            tokenList.push_back(node->token);
            traverse_srcNode_iter(tokenList, node->parent[0], count);
            tokenList.pop_back();
        }

        // 遍历所有API
        void traverseAPI() {
            int count = 0;
            std::unordered_map<std::string, srcServiceNode*>::iterator iter;
            std::vector<std::string> tokenList;
            for (iter = srcServiceMap.begin(); iter != srcServiceMap.end(); iter++) {
                srcServiceNode *srcNode = iter->second;
                tokenList.push_back(srcNode->token);
                if (srcNode->token.compare("front-end") == 0) {
                    std::cout << " " << std::endl;
                }
                for (int i = 0; i < srcNode->pathIndex.size(); i++) {
                    std::vector<int> path = srcNode->pathIndex[i];
                    destServiceNode *destNode = srcNode->parent[i];
                    tokenList.push_back(destNode->token);
                    for (int j = 0; j < path.size(); j++) {
                        traverse_srcNode_iter(tokenList, destNode->parent[path[j]], &count);
                    }
                    tokenList.pop_back();
                }
                tokenList.pop_back();
            }
            std::cout << "API COUNT :" << count << std::endl;
        }

        // 处理token
        void construct_bertStr_iter(std::string &bertStr, valueNode *node, int layer, int mode) {
            // TCP会第一时间到达root
            if (node == this->root) {
                return;
            }
            // HTTP VERSION节点 HTTP METHOD节点
            if (layer < 2) {
                construct_bertStr_iter(bertStr, node->parent[0], layer + 1, mode);
            } else {
                if (mode == 0) {
                    std::unordered_map<std::string, int>::iterator iter = bertTokenMap.find(node->token);
                    if (iter == bertTokenMap.end()) {
                        bertTokenMap.emplace(node->token, -1);
                        bertStr += node->token + " ";
                    }
                } else if (mode == 1) {
                    std::unordered_map<std::string, int>::iterator iter = bertTokenMap.find(node->token);
                    if (iter == bertTokenMap.end()) {
                        // 由于是至底向上 所以可能出现访问相同节点的情况，但这个节点可能先前被替换成* 因此字典里没有
                        //std::cout << "ERROR BERT MAP" << std::endl;
                    } else {
                        //std::cout << "OK" << iter->first << " " << iter->second << std::endl;
                        // 变量 替换为*
                        if (iter->second == 1) {
                            
                            node->token = std::string("/*");
                        }
                    }
                }
                construct_bertStr_iter(bertStr, node->parent[0], layer + 1, mode);
            }
        }

        // 先处理源服务节点和目的服务节点
        void construct_bertStr_inner(std::string &bertStr, srcServiceNode *sNode, int mode) {
            // 从源服务节点开始
            
            for (int i = 0; i < sNode->pathIndex.size(); i++) {
                destServiceNode *dNode = sNode->parent[i];
                std::vector<int> path = sNode->pathIndex[i];
                for (int j = 0; j < path.size(); j++) {
                    construct_bertStr_iter(bertStr, dNode->parent[path[j]], 0, mode);
                }
            }
        }

        // mode = 0为询问bert       
        // mode = 1为询问后替换token为*或不替换
        void construct_bertStr(std::string &bertStr, int mode) {
            for (auto iter = newsrcServiceSet.begin(); iter != newsrcServiceSet.end(); iter++) {
                std::unordered_map<std::string, srcServiceNode*>::iterator srcNodeIter = srcServiceMap.find(*iter);
                srcServiceNode *srcNode = srcNodeIter->second;
                construct_bertStr_inner(bertStr, srcNode, mode);
            }
        }

        // hello 0 mksmkc 1
        void analyze_bert(std::string resultStr) {
            std::string token;
            int i = 0;
            std::unordered_map<std::string, int>::iterator iter;
            while (i < resultStr.length()) {
                while (resultStr[i] != ' ') {
                    token += resultStr[i];
                    i++;
                }
                i++;
                if (resultStr[i] == '1') {
                    iter = bertTokenMap.find(token);
                    if (iter == bertTokenMap.end()) {

                    }
                    //bertTokenMap.emplace(token, 1);
                    iter->second = 1;
                } else if (resultStr[i] == '0') {
                    iter = bertTokenMap.find(token);
                    if (iter == bertTokenMap.end()) {

                    }
                    iter->second = 0;
                    //bertTokenMap.emplace(token, 0);
                    
                }
                
                i++;
                i++;
                token.clear();
            }
        }

        void identifyVar() {

            // 广度优先构造string 问完bert后就能按广度优先替换树节点的token
            std::string bertStr;
        
            construct_bertStr(bertStr, 0);
            // std::cout << "TRAIN - BERT STR: " << bertStr << std::endl;

            // std::string resultStr = ask_bert(bertStr);
            // std::cout << "BERT RESULT :" << resultStr << std::endl;

            // 方便起见 book-info API * 10
            // std::string resultStr("/6 1 /details 0 /1 1 /4 1 /8 1 /9 1 /0 1 /2 1 /5 1 /3 1 /7 1 /reviews 0 /ratings 0 /products 0 /v1 0 /api 0 /productpage 0 /login 0 /logout 0");
            // 方便起见 pitstop API * 1
            // std::string resultStr("/Error 0 /VehicleManagement 0 /New 0 /Home 0 =Ret-717-bOV 1 ?licenseNumber 0 /Details 0 /Register 0 /Index 0 =2025-01-22 1 ?planningDate 0 /WorkshopManagement 0 /CustomerManagement 0 /2025-01-22 1 /RegisterMaintenanceJob 0 =e0bf11d9-4178-4bbf-bc6d-80177816175e 1 &jobId 0 /Finish 0 /FinishMaintenanceJob 0 /About 0 /c1e4b543a53b4072b10e9802f8e51ba7 1 /Ret-717-bOV 1 /vehicles 0 /api 0 /e0bf11d9-4178-4bbf-bc6d-80177816175e 1 /jobs 0 /workshopplanning 0 /finish 0 /refdata 0 /customers 0");
            // 方便起见 pitstop API * 10
            // std::string resultStr("/2025-01-27 1 /New 0 /WorkshopManagement 0 =2025-01-27 1 ?planningDate 0 /Index 0 =84a1c54b-f564-40a6-a4a8-31779351be4c 1 &jobId 0 =2025-01-26 1 /Details 0 /2025-01-26 1 /VehicleManagement 0 /Home 0 =8a053997-7381-459b-85ee-a7eb8f20185d 1 =2025-01-29 1 /Finish 0 =1d1ecabe-7605-4c12-806a-bfc7fd207e11 1 =2025-01-25 1 =2025-01-24 1 =2025-01-23 1 /FinishMaintenanceJob 0 =2025-01-22 1 =26ad1bf5-aae4-4b0a-92c1-8ab9330329c6 1 =5007fa72-5cd2-4d50-bea8-7af58f56cc4d 1 /Error 0 =snW-158-366 1 ?licenseNumber 0 =oas-980-zfh 1 /32029f96ae834d76b7755cde6bb98353 1 /CustomerManagement 0 /ac3279d17df8431eb0cf9bc00aab0564 1 =UIa-457-023 1 =2025-01-31 1 /Register 0 =362d950e-13cc-41cc-ae22-88537e712a9b 1 /2025-01-28 1 =2025-01-30 1 =4c8f253a-67d1-4264-8dc2-d8ab54afb07f 1 /62d0037c9fa74fbc9fa8c5efa727f94c 1 /2025-01-29 1 =59a56147-668c-4606-acfd-60569fa7a26d 1 =030-nbY-671 1 /2025-01-25 1 =879-gqQ-cLK 1 /2025-01-31 1 =YJe-QhJ-208 1 =404-444-768 1 =2025-01-28 1 /2025-01-23 1 =1a2a648c-fba2-444c-8f9a-38516922f48b 1 =afaff0bd-1ace-4810-a840-d29eb2d2cdc1 1 /ecc513dc674141f381e3f7dfb3fab156 1 =Elq-805-131 1 /cfe5aebf0e724288acd4e15909e3b7f4 1 =ZcJ-hIu-022 1 =186-210-Yuj 1 /2654c95b5296434e8f0886293bead1d2 1 /RegisterMaintenanceJob 0 /f87c76257611432fb5b7bc16972084b1 1 /2025-01-24 1 /2025-01-22 1 /About 0 /2025-01-30 1 /b28c5f97563a41a099d24593da4d1da2 1 /6c6578474e8e405b8d9e6e23d260b794 1 /606b748a7c5c405084df0c4dec8a0b85 1 /5007fa72-5cd2-4d50-bea8-7af58f56cc4d 1 /jobs 0 /workshopplanning 0 /api 0 /finish 0 /84a1c54b-f564-40a6-a4a8-31779351be4c 1 /362d950e-13cc-41cc-ae22-88537e712a9b 1 /1d1ecabe-7605-4c12-806a-bfc7fd207e11 1 /afaff0bd-1ace-4810-a840-d29eb2d2cdc1 1 /26ad1bf5-aae4-4b0a-92c1-8ab9330329c6 1 /vehicles 0 /refdata 0 /customers 0 /59a56147-668c-4606-acfd-60569fa7a26d 1 /4c8f253a-67d1-4264-8dc2-d8ab54afb07f 1 /030-nbY-671 1 /1a2a648c-fba2-444c-8f9a-38516922f48b 1 /8a053997-7381-459b-85ee-a7eb8f20185d 1 /Elq-805-131 1 /YJe-QhJ-208 1 /oas-980-zfh 1 /186-210-Yuj 1 /879-gqQ-cLK 1 /UIa-457-023 1 /404-444-768 1 /snW-158-366 1 /ZcJ-hIu-022 1");
            // sockshop API * 1
            // std::string resultStr("/67a725e8f7446b0001db96c8 1 /cards 0 /addresses 0 /67a48450f7446b0001db95db 1 /customers 0 /67a715edf7446b0001db963b 1 /login 0 /register 0 /67a725eaf7446b0001db96c9 1 /items 0 /carts 0 /3395a43e-2d88-40de-b95f-e00e1502085b 1 =aqHYTkKj_UnelLEQkKE1B1pnqEaE4rTk 1 ?sessionId 0 /merge 0 /catalogue 0 /size 0 /tags 0 /colourful_socks.jpg 1 /images 0 /67a4852e9a77a30006d76055 1 /orders 0 =67a48450f7446b0001db95db 1 &custId 0 =date 1 ?sort 0 /customerId 0 /search 0 /card 0 /update 0 /cart 0 /address 0 /67a48511f7446b0001db95e2 1 /shipping 0 /paymentAuth 0");
            // sockshop API * 10
            std::string resultStr("/67a717a0f7446b0001db96a3 1 /cards 0 /address 0 /67a715f7f7446b0001db9653 1 /addresses 0 /login 0 /67a717a1f7446b0001db96a6 1 /orders 0 /customers 0 /67a717abf7446b0001db96c2 1 /cart 0 /catalogue 0 /update 0 /67a717acf7446b0001db96c3 1 /register 0 /classic.jpg 1 /images 0 /67a717a8f7446b0001db96bd 1 /67a717aaf7446b0001db96c0 1 /67a7162ff7446b0001db965f 1 /67a6b45f9a77a30006d76060 1 /67a6b4589a77a30006d7605a 1 /3395a43e-2d88-40de-b95f-e00e1502085b 1 /837ab141-399e-4c1f-9abc-bace40296bac 1 /67a717aef7446b0001db96c5 1 /d3588630-ad8e-49df-bbd7-3167f7efb246 1 /youtube_1.jpeg 1 /card 0 /zzz4f044-b040-410d-8ead-4de0446aec7e 1 /819e1fbf-8b7e-4f6d-811f-693534916a8b 1 /67a715f3f7446b0001db9647 1 /67a717a7f7446b0001db96bc 1 /catsocks.jpg 1 /67a717adf7446b0001db96c4 1 /67a715f1f7446b0001db9644 1 /67a717a2f7446b0001db96a9 1 /WAT.jpg 1 /67a7179ff7446b0001db96a0 1 /67a717a3f7446b0001db96af 1 /67a717a3f7446b0001db96ac 1 /67a715f7f7446b0001db9656 1 /size 0 /510a0d7e-8e83-4193-b483-e27e09ddc34d 1 /67a717abf7446b0001db96c1 1 /67a717a6f7446b0001db96b8 1 /808a2de1-1aaa-4c25-a9b9-6612e8f29a38 1 /colourful_socks.jpg 1 /bit_of_leg_1.jpeg 1 /puma_1.jpeg 1 /a0a4f044-b040-410d-8ead-4de0446aec7e 1 /67a717a6f7446b0001db96bb 1 /67a6b45e9a77a30006d7605f 1 /tags 0 /67a7162ef7446b0001db965c 1 /67a4852e9a77a30006d76055 1 /67a6b45a9a77a30006d7605b 1 /67a717a5f7446b0001db96b5 1 /67a717a9f7446b0001db96be 1 /cross_1.jpeg 1 /67a6b45b9a77a30006d7605c 1 /67a717a9f7446b0001db96bf 1 /67a6b501f7446b0001db9613 1 /67a717a4f7446b0001db96b2 1 /67a6b45c9a77a30006d7605d 1 /67a6b45d9a77a30006d7605e 1 /67a715f6f7446b0001db9650 1 /67a715f4f7446b0001db964a 1 /67a715f5f7446b0001db964d 1 =67a6b458f7446b0001db95f8 1 &custId 0 =date 1 ?sort 0 /customerId 0 /search 0 =67a6b45cf7446b0001db9601 1 =67a6b460f7446b0001db960a 1 =67a6b460f7446b0001db960d 1 =67a6b45ef7446b0001db9607 1 =67a6b45af7446b0001db95fb 1 =67a6b45bf7446b0001db95fe 1 =67a6b45df7446b0001db9604 1 =67a6b457f7446b0001db95f5 1 =67a48450f7446b0001db95db 1 /67a6b460f7446b0001db960a 1 /67a6b45ef7446b0001db9607 1 /67a6b45df7446b0001db9604 1 /67a6b458f7446b0001db95f8 1 /67a6b457f7446b0001db95f5 1 /67a6b45bf7446b0001db95fe 1 /67a6b45cf7446b0001db9601 1 /67a6b45af7446b0001db95fb 1 /67a48450f7446b0001db95db 1 /67a6b460f7446b0001db960d 1 =0AwlxB4qQEyFR6v1dsWOpkYs03YskwyO 1 ?sessionId 0 /merge 0 /carts 0 /items 0 /67a6b45ff7446b0001db9609 1 /67a6b459f7446b0001db95fa 1 /67a6b45df7446b0001db9606 1 /67a6b457f7446b0001db95f7 1 /67a6b45df7446b0001db9603 1 /67a48511f7446b0001db95e2 1 /67a6b45bf7446b0001db9600 1 /67a6b45af7446b0001db95fd 1 /shipping 0 /paymentAuth 0");

            // 分析bert返回的预测结果
            analyze_bert(resultStr);
            std::string temp("");
            construct_bertStr(temp, 1);
            newsrcServiceSet.clear();
            return ;
        }

        // 应该只有目的服务节点
        void merge_srcPathIndex(valueNode *mergeNode, valueNode *parentNode, valueNode *firstNode) {
            int parentIndex = -1;
            // 避免和第一个重复
            bool isFirstNode = false;
            int firstMergeNodeParentIndex = -1;
            for (int i = 0; i < mergeNode->parent.size(); i++) {
                if (mergeNode->parent[i] == parentNode && isFirstNode == true) {
                    parentIndex = i;
                    break;
                } else if (mergeNode->parent[i] == parentNode) {
                    isFirstNode = true;
                    firstMergeNodeParentIndex = i;
                }
            }
            // 为了链接那些一开始没有连的路径
            int firstNodeParentIndex = -1;
            for (int i = 0; i < firstNode->parent.size(); i++) {
                if (firstNode->parent[i] == parentNode) {
                    firstNodeParentIndex = i;
                    break;
                }
            }
            // 找到被合并节点其父节点对应哪个索引
            // 需要修改源节点的pathIndex
            // 遍历每个源节点
            for (int i = 0; i < mergeNode->children.size(); i++) {
                srcServiceNode *srcNode = mergeNode->children[i];

                // destIndex为源服务节点指向目的服务节点的父索引
                int destIndex = -1;
                // 找到srcNode指向目的节点的pathIndex数组
                for (int j = 0; j < srcNode->parent.size(); j++) {
                    if (srcNode->parent[j] == mergeNode) {
                        destIndex = j;
                        break;
                    }
                }
                // 删除源服务节点指向被合并节点的索引
                bool isDelete = false;
                for (int j = 0; j < srcNode->pathIndex[destIndex].size(); j++) {
                    if (srcNode->pathIndex[destIndex][j] == parentIndex) {
                        srcNode->pathIndex[destIndex].erase(srcNode->pathIndex[destIndex].begin() + j);
                        isDelete = true;
                        break;
                    }    
                }
                
                // 为了让那些没路径但删了路径的的连上（代表有重复的节点但是第一个路径没连）
                bool isExistFirstNode = false;
                if (isDelete) {
                    for (int j = 0; j < srcNode->pathIndex[destIndex].size(); j++) {
                        if (srcNode->pathIndex[destIndex][j] == firstNodeParentIndex) {
                            isExistFirstNode = true;
                            break;
                        }
                    }
                    // 没连 添加上
                    if (isExistFirstNode == false) {
                        srcNode->pathIndex[destIndex].push_back(firstNodeParentIndex);
                    }
                }
                
                // 由于从中间删了一个节点，因此要把大于这个节点的索引减一
                for (int j = 0; j < srcNode->pathIndex[destIndex].size(); j++) {
                    if (srcNode->pathIndex[destIndex][j] >= parentIndex) {
                        srcNode->pathIndex[destIndex][j]--;
                    }
                }


                
            }
            
            mergeNode->parent.erase(mergeNode->parent.begin() + parentIndex);
        }

        // 只需要在最后的url token处理srcNode的pathIndex就可
        // 因为同一路径合并后其最后token肯定要合并
        void merge_node_iter(valueNode *node) {
            // 遍历到目的服务节点就可以结束
            if (dynamic_cast<destServiceNode*>(node)) {
                return;
            }

            std::unordered_map<std::string, int> deduplicateMap;
            // 存储下一次要遍历的节点（没被删除的节点)
            std::unordered_map<std::string, int>::iterator iter;
            // 删除 当前节点指向要删除的节点
            std::vector<valueNode*> deleteList;
            std::vector<int> deleteDestNodeLinkList;
            for (int i = 0; i < node->children.size(); i++) {
                valueNode *mergeNode = node->children[i];
                iter = deduplicateMap.find(mergeNode->token);
                if (iter == deduplicateMap.end()) {
                    deduplicateMap.emplace(mergeNode->token, i);
                // 有重复的子节点
                } else {
                    int firstIndex = iter->second;
                    valueNode *firstNode = node->children[firstIndex];
                    // 两个都是目的节点 可以合并为一个目的节点
                    if (dynamic_cast<destServiceNode*>(firstNode) && dynamic_cast<destServiceNode*>(mergeNode)) {
                        merge_srcPathIndex(mergeNode, node, firstNode);
                        deleteDestNodeLinkList.push_back(i);
                    // 都不是目的节点 可以合并为一个节点 
                    } else if (!dynamic_cast<destServiceNode*>(firstNode) && !dynamic_cast<destServiceNode*>(mergeNode)) {
                        deleteList.push_back(mergeNode);
                        // 把要删除的节点的子节点 成为 没被删除的节点的子节点
                        for (int j = 0; j < mergeNode->children.size(); j++) {
                            firstNode->children.push_back(mergeNode->children[j]);
                            // 因为有可能子节点的子节点是目的节点 所以有可能parent有多个路径
                            for (int k = 0; k < mergeNode->children[j]->parent.size(); k++) {
                                if (mergeNode->children[j]->parent[k] == mergeNode) {
                                    mergeNode->children[j]->parent.erase(mergeNode->children[j]->parent.begin() + k);
                                    mergeNode->children[j]->parent.insert(mergeNode->children[j]->parent.begin() + k, firstNode);
                                    break;
                                }
                            }
                        }              
                        // 一个目的节点 一个token节点 不需要合并
                    } else {

                    }
                        // mergeNode->children.clear();
                    
                }
            }

            //  统一删除 node与目的服务节点链接
            // 由于都是相同地址，所以搞一个表用来防止第一个节点被删
            for (int i = 0; i < deleteDestNodeLinkList.size(); i++) {
                node->children.erase(node->children.begin() + deleteDestNodeLinkList[i] - i);
            }
        
            // 统一删除 因为可能有目的服务节点 不能删目的服务节点 所以要过滤一遍
            for (int i = 0; i < deleteList.size(); i++) {
                for (int j = 0; j < node->children.size(); j++) {
                    if (node->children[j] == deleteList[i]) {
                        node->children.erase(node->children.begin() + j);
                        break;
                    }
                }
                valueNode* temp = deleteList[i];
                delete temp;
            }

            // for (auto iter = iterNodeSet.begin(); iter != iterNodeSet.end(); iter++) {
            //     merge_node_iter(*iter);
            // }
            for (int i = 0; i < node->children.size(); i++) {
                merge_node_iter(node->children[i]);
            }
        }   

        // 在询问bert后 有些节点变为* 需要合并
        // 具体是遍历每个节点的子节点 有重复的*就合并*的子节点
        void merge_node() {
            std::cout << "MERGING NODES..." << std::endl;
            merge_node_iter(this->root);
        }

        /*
        void traverse_test_iter(valueNode* node) {
            std::cout << node->token << ":";
            for (int i = 0; i < node->children.size(); i++) {
                std::cout << node->children[i]->token << " ";
            }
            std::cout << std::endl;
            for (int i = 0; i < node->children.size(); i++) {
                traverse_test_iter(node->children[i]);
            }
        }

        void traverse_test() {
            std::cout << this->root->token << ":";
            for (int i = 0; i < this->root->children.size(); i++) {
                std::cout << this->root->children[i]->token << " ";
            }
            std::cout << std::endl;

            for (int i = 0; i < this->root->children.size(); i++) {
                traverse_test_iter(this->root->children[i]);
            }
        }

        void test_src() {
            for (auto iter = srcServiceMap.begin(); iter != srcServiceMap.end(); iter++) {
                std::cout << "fk : " << iter->second->token << " : " << iter->second->pathIndex.size() << std::endl;
                for (int i = 0; i < iter->second->pathIndex.size(); i++) {
                    std::cout << "SIZE : " << iter->second->pathIndex[i].size() << std::endl;
                    for (int j = 0; j < iter->second->pathIndex[i].size(); j++) {
                        std::cout << iter->second->pathIndex[i][j] << " ";
                    }
                    std::cout << std::endl;
                }
            }
        }
        */
        void extractAPI() {
            // 询问BERT识别变量
            identifyVar();
            // 合并节点
            merge_node();
            traverseAPI();
            
        }
        
        void make_word_dict_iter(std::string &ruleStr, std::unordered_map<std::string, int> &urlWordDict, valueNode *node, int &index, std::vector<std::string> &tokenList, std::vector<std::string> &tokenStrList) {
            if (node == this->root) {
                // 源ip 现在没有目的ip了
                ruleStr += tokenList[0] + ':';
                // HTTP 独有
                if (tokenList.size() > 1) {
                    // HTTP METHOD
                    ruleStr += tokenList[2] + ':';
                    // HTTP URL
                    for (int i = tokenList.size() - 1; i > 2; i--) {
                        ruleStr += tokenList[i] + ':';
                    }
                    // HTTP VERSION
                    ruleStr += tokenList[1];
                }
                ruleStr += ' ';

                // 打印token
                std::cout << "TRAIN - TOKEN List : ";
                std::cout << tokenStrList[0] + ' ';
                std::cout << tokenStrList[2] + ' ';
                for (int i = tokenStrList.size() - 1; i > 2; i--) {
                    std::cout << tokenStrList[i];
                }
                std::cout << " ";
                std::cout << tokenStrList[1] << std::endl;
                return;
            }
            auto iter = urlWordDict.find(node->token);
            if (iter == urlWordDict.end()) {
                urlWordDict.emplace(node->token, index++);
            }
            // 为什么要重新找，因为可能上面是没有找到的
            iter = urlWordDict.find(node->token);
            tokenList.push_back(std::to_string(iter->second));
            tokenStrList.push_back(node->token);
            make_word_dict_iter(ruleStr, urlWordDict, node->parent[0], index, tokenList, tokenStrList);
            tokenStrList.pop_back();
            tokenList.pop_back();
        }

        void make_word_dict(std::unordered_map<std::string, std::vector<std::string>> &serviceIpDict, std::string &ruleStr, 
                            std::unordered_map<std::string, int> &urlWordDict, std::unordered_map<std::string, int> &srcIpDict,
                            destServiceNode *destNode, std::unordered_map<std::string, int> &globalSrcIpDict) {
            // 用来debug 打印规则表的字典形式
            std::vector<std::string> tokenList;
            // 用来debug 打印规则表的字符形式
            std::vector<std::string> tokenStrList;
            // 每个token对应字典的索引
            int index = 0;
            // 找到目的服务的对应节点IP
            auto serviceIpIter = serviceIpDict.find(destNode->token);
            if (serviceIpIter == serviceIpDict.end()) {
                std::cout << "TRAIN - MAKE WORD DICT: ERROR FIND SERVICE IP" << std::endl;
                return;
            }
            // 这里index不++是因为目的服务有几个ip 那么几个ip对应同一个字典的index
            // 目的ip就不用加到字典了，因为作为规则表索引用了
            // tempIpIndex用来找几个相同服务的ip之前有没有出现过
            for (auto serviceIpVecIter = serviceIpIter->second.begin(); serviceIpVecIter != serviceIpIter->second.end(); serviceIpVecIter++) {
                
                // struct in_addr addr;
                // inet_pton(AF_INET, (*serviceIpVecIter).c_str(), &addr);
                // // std::cout << "TRAIN - IP2:" << inet_ntoa(addr) << std::endl;
                // ipWordDict.emplace(std::to_string(addr.s_addr), index);

                // 目的服务需要把所有ip都传 源服务不用 （因为目的服务要用来在工作节点索引字典表）
                // RULE就先不转字节序的int的string形式 为了方便在工作节点处理
                ruleStr += (*serviceIpVecIter) + ' ';
            }
            // 标识目的服务的终结
            ruleStr += '|';
            // for (auto iter = wordDict.begin(); iter != wordDict.end(); iter++) {
            //     std::cout << "TRAIN - MAKE WORD DICT:" << iter->first << " " << iter->second << std::endl;
            // }

            for (int i = 0; i < destNode->children.size(); i++) {
                srcServiceNode *srcNode = destNode->children[i];
                serviceIpIter = serviceIpDict.find(srcNode->token);
                if (serviceIpIter == serviceIpDict.end()) {
                    std::cout << "TRAIN - MAKE WORD DICT - ERROR FIND SERVICE IP" << std::endl;
                    return;
                }

                auto ipIter = serviceIpIter->second.begin();
                auto srcIpIter = globalSrcIpDict.find(*ipIter);
                tokenStrList.push_back(*ipIter);
                int srcIpDictIndex = srcIpIter->second;
                // 由于源服务可能有多个ip  
                std::string ip_str;
                for (auto serviceIpVecIter = serviceIpIter->second.begin(); serviceIpVecIter != serviceIpIter->second.end(); serviceIpVecIter++) {
                    // 转成be32的形式存储在字典里
                    struct in_addr addr;
                    inet_pton(AF_INET, (*serviceIpVecIter).c_str(), &addr);
                    ip_str = std::to_string(addr.s_addr);
                    // 源服务应该也不用index++
                    srcIpDict.emplace(ip_str, srcIpDictIndex);

                }
                
                auto wordDictiter = srcIpDict.find(ip_str);
                tokenList.push_back(std::to_string(wordDictiter->second));
                // 找目的服务相对于源服务的父索引
                int destIndex = -1;
                for (int j = 0; j < srcNode->parent.size(); j++) {
                    if (srcNode->parent[j] == destNode) {
                        destIndex = j;
                    }
                }
                std::vector<int> path = srcNode->pathIndex[destIndex];
                for (int j = 0; j < path.size(); j++) {
                    make_word_dict_iter(ruleStr, urlWordDict, destNode->parent[path[j]], index, tokenList, tokenStrList);
                }
                tokenList.pop_back();
                tokenStrList.pop_back();
            }
            ruleStr += '\n';
            // std::cout << "TRAIN - RULE STR : " << ruleStr << std::endl;
        }

        void construct_ruleStr(std::unordered_map<std::string, std::vector<std::string>> &serviceIpDict, std::string &ruleStr, 
                            std::unordered_map<std::string, int> &urlWordDict, std::unordered_map<std::string, int> &srcIpDict, 
                            std::string serviceName, std::unordered_map<std::string, int> &globalSrcIpDict) {

            // 在树中找对应目的节点
            auto iter = destServiceMap.find(serviceName);
            if (iter != destServiceMap.end()) {
                destServiceNode *destNode = iter->second;
                make_word_dict(serviceIpDict, ruleStr, urlWordDict, srcIpDict, destNode, globalSrcIpDict);
                
            } else {
                std::cout << "TRAIN - CONSTRUCT RULESTR | Error find dest service : " << serviceName << std::endl;
            }
        }

        void construct_ipWordDict(std::string &ruleStr, std::unordered_map<std::string, int> &srcIpDict) {
            if (srcIpDict.size() > 0) {
                for (auto iter = srcIpDict.begin(); iter != srcIpDict.end(); iter++) {
                    ruleStr += iter->first + ' ' + std::to_string(iter->second) + ' ';
                }
                ruleStr += '|';
            }
        }

        // 让ruleStr携带字典信息
        void construct_urlWordDict(std::string &ruleStr, std::unordered_map<std::string, int> &urlWordDict) {
            if (urlWordDict.size() > 0) {
                for (auto iter = urlWordDict.begin(); iter != urlWordDict.end(); iter++) {
                    ruleStr += iter->first + ' ' + std::to_string(iter->second) + ' ';
                }
                ruleStr += '\n';
            }   
            //std::cout << " POLICY RULE : " << ruleStr << std::endl;
        }

        void test_construct_ruleStr(std::string &ruleStr, std::unordered_map<std::string, std::vector<std::string>> &serviceIpDict) {
            
        }

    private:
        valueNode *root;
        // 变量统计字典
        std::unordered_map<std::string, int> varDict;
        // 记录各元素出现次数 构造生成数据约束
        // 0 源服务
        // 1 目的服务
        // 2 httpVersion
        // 3 httpMethod
        // 4 httpUrl
        // 去重重复元素
        std::vector<std::unordered_set<std::string>> wordSet;

        // 目的服务节点表
        std::unordered_map<std::string, destServiceNode*> destServiceMap;
        // 源服务节点表
        std::unordered_map<std::string, srcServiceNode*> srcServiceMap;

        // 新的源服务节点集合 用来只访问新服务相关的path 这样就减少访问BERT的节点数
        std::unordered_set<std::string> newsrcServiceSet;

        // 缓存询问bert后的token与其预测值 
        // -1代表存在重复token 但没预测
        // 0代表预测为不是变量
        // 1代表预测为变量
        std::unordered_map<std::string, int> bertTokenMap;
};

// 统计数据的token数
void token_statistic(std::vector<std::vector<std::string>> *dataVecAll, parserTree *tree) {
    int token_size = 0;
    if (dataVecAll != nullptr) {
        for (auto iter = dataVecAll->begin(); iter != dataVecAll->end(); iter++) {
            // 目前HTTP数据有5个大token
            for (int i = 0; i < 5; i++) {
                if (i == 1) {
                    std::vector<std::string> urlToken;
                    std::vector<char> delim = {'/', '&', '?', '='};
                    split_url_token(urlToken, (*iter)[i], delim);
                    token_size += urlToken.size();
                } else {
                    token_size += 1;
                }
            } 
        }
        std::cout << "TOKEN STAT | ORIGIN TOKEN SIZE : " << token_size << std::endl;
    } else if (tree != nullptr) {
        token_size += tree->stat_token();
        std::cout << "TOKEN STAT | TREE TOKEN SIZE : " << token_size << std::endl;
    }
    
}

// 测试没有前缀树 bert的压缩token效率
void token_statistic_without_tree(std::vector<std::vector<std::string>> *dataVecAll) {
    std::string bertStr;
    int count = 0;
    // for (auto iter = (*dataVecAll).begin(); iter != (*dataVecAll).end(); iter++) {
    //     for (int i = 0; i < (*iter).size(); i++) {
    //         if (i == 1) {
    //             std::vector<std::string> tokens;
    //             std::vector<char> splitCharSet = {'/', '=', '?', '&'};
    //             split_url_token(tokens, (*iter)[i], splitCharSet);
    //             count += tokens.size();
    //             for (int j = 0; j < tokens.size(); j++) {
    //                 bertStr += tokens[j] + ' ';
    //             }
    //         }
    //     }
    // }
    // std::cout << "TRAIN - statistic without tree - bertStr:" << bertStr << std::endl;
    // std::cout << "TRAIN - statistic without tree - token count:" << count << std::endl;
    // // 测没有用set去重token时bert处理效率
    // ask_bert(bertStr);

    bertStr.clear();
    count = 0;
    // 去重token
    std::unordered_set<std::string> mySet;
    for (auto iter = (*dataVecAll).begin(); iter != (*dataVecAll).end(); iter++) {
        for (int i = 0; i < (*iter).size(); i++) {
            if (i == 1) {
                std::vector<std::string> tokens;
                std::vector<char> splitCharSet = {'/', '=', '?', '&'};
                split_url_token(tokens, (*iter)[i], splitCharSet);
                count += tokens.size();
                for (int j = 0; j < tokens.size(); j++) {
                    auto setIter = mySet.find(tokens[j]);
                    if (setIter == mySet.end()) {
                        mySet.insert(tokens[j]);
                    }
                }
            }
        }
    }
    std::cout << "TRAIN - statistic without tree - deduplicated token count:" << mySet.size() << std::endl;

    for (auto iter = mySet.begin(); iter != mySet.end(); iter++) {
        bertStr += (*iter) + ' ';
    }
    std::cout << "TRAIN - statistic without tree - deduplicated bertStr:" << bertStr << std::endl;
    std::string bertResult = ask_bert(bertStr);
    std::cout << "TRAIN - statistic without tree - deduplicated bertResult:" << bertResult << std::endl;
    std::unordered_map<std::string, std::string> myMap;
    std::string key, value;
    for (int i = 0; i < bertResult.length(); i++) {
        while (bertResult[i] != ' ') {
            key += bertResult[i];
            i++;
        }
        i++;
        while (bertResult[i] != ' ' && bertResult[i] != '\n') {
            value += bertResult[i];
            i++;
        }
        myMap.emplace(key, value);
        key.clear();
        value.clear();
    }

    // std::cout << "TRAIN - token statistic without tree - mymap:";
    // for (auto iter = myMap.begin(); iter != myMap.end(); iter++) {
    //     std::cout << iter->first << " " << iter->second << "|";
    // }
    // std::cout << std::endl;

    // 替换bert预测结果
    std::vector<std::vector<std::string>> dataVecAllBert;
    std::string deplicatedStr;
    std::unordered_set<std::string> deplicatedSet;
    std::vector<std::string> tokenVec;
    for (int i = 0; i < (*dataVecAll).size(); i++) {
        // std::cout << "TRAIN - token statistic without tree hi:";
        for (int j = 0; j < (*dataVecAll)[i].size(); j++) {
            // std::cout << (*dataVecAll)[i][j] << " ";
            if (j != 1) {
                deplicatedStr += (*dataVecAll)[i][j] + ' ';
                tokenVec.push_back((*dataVecAll)[i][j]);
            } else {
                std::vector<std::string> atokens;
                std::vector<char> splitCharSet = {'/', '=', '?', '&'};
                split_url_token(atokens, (*dataVecAll)[i][j], splitCharSet);
                for (int j = 0; j < atokens.size(); j++) {
                    auto iter = myMap.find(atokens[j]);
                    if (iter != myMap.end()) {
                        if (iter->second.compare("0") == 0) {
                            deplicatedStr += iter->first + "/";
                            tokenVec.push_back(iter->first);
                        } else {
                            deplicatedStr += "*/";
                            tokenVec.push_back("*");
                        }
                    }
                }
            }
        }    
        auto deplicatedSetIter = deplicatedSet.find(deplicatedStr);
        if (deplicatedSetIter == deplicatedSet.end()) {
            deplicatedSet.insert(deplicatedStr);
            dataVecAllBert.push_back(tokenVec);
        }    
        tokenVec.clear();
        deplicatedStr.clear();
    }

    count = 0;
    for (int i = 0; i < dataVecAllBert.size(); i++) {
        std::cout << "TRAIN - token statistic without tree:";
        for (int j = 0; j < dataVecAllBert[i].size(); j++) {
            std::cout << dataVecAllBert[i][j] << " ";
            count++;
        }
        std::cout << std::endl;
    }
    std::cout << "TRAIN - token statistic without tree count - " << count << std::endl;
}

void make_policy(parserTree &tree, std::string fname, std::vector<int> &dictConstraint) {

    std::string filename = fname + "_mapped";
    std::fstream file1;
    file1.open(filename, std::ios::binary | std::ios::in);
    if (file1.is_open()) {
        std::string protocol;
        std::string srcService, destService;
        std::string httpMethod, httpUrl, httpVersion;
        // 存储每行数据 TCP长度为3 HTTP 长度为5
        std::vector<std::string> dataVec;
        // 存放所有数据 用于统计实验
        std::vector<std::vector<std::string>> dataVecAll;
        // TCP : 1  HTTP : 2
        int mode = 0;
        while (file1 >> protocol) {
            // 分析协议及其数据
            if (protocol.compare("TCP") == 0) {
                //std::cout << "YES" << std::endl;
                file1 >> srcService >> destService;
                dataVec.push_back(protocol);
                dataVec.push_back(srcService);
                dataVec.push_back(destService);
                mode = 1;
            } else if (protocol.compare("GET") == 0 || protocol.compare("POST") == 0 || protocol.compare("DELETE") == 0 || 
                    protocol.compare("HEAD") == 0 || protocol.compare("PUT") == 0 || protocol.compare("PATCH") == 0) {
                file1 >> httpUrl >> httpVersion >> srcService >> destService;
                dataVec.push_back(protocol);
                dataVec.push_back(httpUrl);
                dataVec.push_back(httpVersion);
                dataVec.push_back(srcService);
                dataVec.push_back(destService);
                dataVecAll.push_back(dataVec);
                mode = 2;
            } else {
                std::cout << "MAKE POLICY : UNKNOWN PROTOCOL" << std::endl;
            }
            /*
            for (int i = 0 ; i < dataSet.size(); i++) {
                std::cout << dataSet[i] << " ";
            }
            std::cout << std::endl;
            */
            // 插入进树
            tree.insert(dataVec, mode);
            dataVec.clear();
        }
        // 统计token数
        token_statistic(&dataVecAll, nullptr);
        // token_statistic_without_tree(&dataVecAll);
        token_statistic(nullptr, &tree);
        std::cout << "---------------------------" << std::endl;
        //tree.delete_service("catalogue");
        //tree.traverse_tree();
        
        //tree.traverse_tree();
        // tree.traverseAPI();
        // 打印树有的模板
        std::cout << "---------------------------" << std::endl;
        std::cout << "ask BERT" << std::endl;
        tree.extractAPI();
        // 统计token数
        token_statistic(nullptr, &tree);
        //tree.test();
        std::cout << "---------------------------" << std::endl;
        //tree.delete_service(std::string("carts"));
        // tree.traverse_tree();
        // tree.traverseAPI();
        //tree.traverse_tree();
        //tree.traverse_srcServiceInfo(std::string("outcluster"));

    }
    return;
}

// 发送规则到工作节点 (目的节点)
void construct_rule(std::unordered_map<std::string, std::vector<std::string>> &serviceIpDict, parserTree &tree, std::string &ruleStr, std::string serviceName, 
                    std::unordered_map<std::string, int> &globalSrcIpDict) {
    // 两个字典 一个用来查url的token,一个用来int查ip
    std::unordered_map<std::string, int> urlWordDict;
    std::unordered_map<std::string, int> srcIpDict;
    tree.construct_ruleStr(serviceIpDict, ruleStr, urlWordDict, srcIpDict, serviceName, globalSrcIpDict);
    
    tree.construct_ipWordDict(ruleStr, srcIpDict);
    tree.construct_urlWordDict(ruleStr, urlWordDict);
    return;
}

void test_construct_rule(parserTree &tree, std::string &ruleStr, std::unordered_map<std::string, std::vector<std::string>> &serviceIpDict) {
    tree.test_construct_ruleStr(ruleStr, serviceIpDict);
    return;
}


void generate_positive_data(parserTree &tree, std::unordered_map<std::string, int> &wordDict, 
                            std::vector<std::string>& pDataFpSet) {
    //tree.getPositiveData(wordDict, pDataFpSet);

}


/*
void generate_positive_data(std::unordered_map<std::string, int> &wordDict, std::vector<std::string>& pDataFingerprintStr) {
    std::string sServiceIp, sServicePort, dServiceIp, dServicePort, httpMethod, httpVersion;
    std::vector<std::string> httpUrlTokens;
    // fileData 一行为所有数据同一属性的值，一列为一个数据不同属性的值
    // 初始化
    std::unordered_map<std::string, int>::iterator iter;
    for (int j = 0; j < fileData[0].size(); j++) {
        for (int i = 0; i < fileData.size(); i++) {
            // url
            if (i == 4) {
                std::vector<std::string> tokens;
                std::vector<char> splitCharSet = {'/', '&', '?', '='};
                split_url_token(tokens, fileData[i][j], splitCharSet);
                for (int k = 0; k < tokens.size(); k++) {
                    
                    if (tokens[k].find('=') != std::string::npos) {
                        tokens[k] = tokens[k].substr(1, tokens[k].length());
                    }
                    // 字典有
                    if (iter = dict.space.find(tokens[k]); iter != dict.space.end()) {
                        // 有冲突是这种数据，应该没问题
                        // outcluster carts GET HTTP/1.1 /carts/57a98d98e4b00679b4a830b2/items
                        if (dataList[j][iter->second] > 0) {
                            //std::cout << "word vector dimension value > 1 : " << tokens[k] << std::endl;
                        }
                        dataList[j][iter->second]++;
                        if (pDataFingerprintStr[j].length() == 0) {
                            pDataFingerprintStr[j] += std::to_string(iter->second);
                        } else {
                            pDataFingerprintStr[j] += ":" + std::to_string(iter->second);
                        }   
                    } else {
                        // 遇到变量, "*"维度++
                        iter = dict.space.find(std::string(1, '*'));
                        dataList[j][iter->second]++;
                        if (pDataFingerprintStr[j].length() == 0) {
                            pDataFingerprintStr[j] += std::to_string(iter->second);
                        } else {
                            pDataFingerprintStr[j] += ":" + std::to_string(iter->second);
                        }
                    }
                }
            } else if (i < 5) { // httpVersion先不考虑 目前考虑
                // 字典有
                if (iter = dict.space.find(fileData[i][j]); iter != dict.space.end()) {
                    // 除url应该都不可以有多过1?
                    // outcluster carts GET HTTP/1.1 /carts/57a98d98e4b00679b4a830b2/items
                    if (dataList[j][iter->second] == 1) {
                        std::cout << "non-url already dimension = 1" << std::endl;
                    }
                    dataList[j][iter->second] = 1;
                    if (pDataFingerprintStr[j].length() == 0) {
                        pDataFingerprintStr[j] += std::to_string(iter->second);
                    } else {
                        pDataFingerprintStr[j] += ":" + std::to_string(iter->second);
                    }
                } else {
                    std::cout << "Dict can't find token(non-url)" << std::endl;
                }
            }
        }
    }
}
*/
/*
void generate_negative_data(struct myDict &wordDict, std::vector<int> &dictTokenCount,
                            std::vector<std::string> &nDataFingerprintStr) {
    int dataCount = nDataFingerprintStr.size();
    srand(std::time(0));
    for (int i = 0; i < dataCount; i++) {
        // sService
        int a = 0;
        int b = dictTokenCount[0];
        int randIndex = (rand() % (b - a)) + a;
        nDataFingerprintStr[i] += std::to_string(randIndex);

        // dService
        a = 0;
        b = dictTokenCount[0] + a;
        randIndex = (rand() % (b - a)) + a;
        nDataFingerprintStr[i] += ":" + std::to_string(randIndex);

        // httpMethod
        a = b;
        b = dictTokenCount[1] + a;
        randIndex = (rand() % (b - a)) + a;
        nDataFingerprintStr[i] += ":" + std::to_string(randIndex);

        // httpVersion
        a = b;
        b = dictTokenCount[2] + a;
        randIndex = (rand() % (b - a)) + a;
        nDataFingerprintStr[i] += ":" + std::to_string(randIndex);

        // httpUrl
        // 控制生成的Url的token个数
        int randUrlTokensCount = rand() % 5;
        a = b;
        b = dictTokenCount[3] + a;
        for (int j = 0; j < randUrlTokensCount; j++) {
            randIndex = (rand() % (b - a)) + a;
            nDataFingerprintStr[i] += ":" + std::to_string(randIndex);
        }
        
    }
}
*/
/*
        // 给dict添加url中的token 遍历树的节点
        void update_dict_token(struct myDict& dict) {
            std::queue<valueNode*> q;
            q.push(this->root);
            while (!q.empty()) {
                valueNode *node = q.front();
                q.pop();
                
                auto iter = dict.space.find(node->token);
                // 字典没有 添加token到字典上
                if (iter == dict.space.end()) {
                    dict.space.emplace(node->token, dict.index++);
                }
                for (int i = 0; i < node->children.size(); i++) {
                    q.push(node->children[i]);
                }
            }
        }
*/


/*
// 大概fingerprint会跟着树结构构建 dService:httpVersion:httpMethod:httpUrl:sService
        void getPositiveData(std::unordered_map<std::string, int> &wordDict, std::vector<std::string>& pDataFpSet) {
            std::vector<int> pDataFp;
            getPositiveDataRecur(wordDict, pDataFpSet, this->root, pDataFp);
        }
        
        void delete_dService_node2(valueNode *node) {
            // 只需要查看第一个parent 因为插入时保证了子节点只有一个父节点
            // 到根节点或者节点有多个child 这两种情况不能删除节点
            valueNode *tmpNode;
            std::vector<valueNode*>::iterator iter;
            while (node != this->root && node->children.size() == 1) {
                //std::cout << "YES" << std::endl;
                tmpNode = node;
                node = node->parent[0];
                if (node->children.size() == 1) {
                    node->children.clear();
                }
                else {
                    std::cout << "DELETE " << node->token << " " << node->children.size() << std::endl;
                    for (iter = node->children.begin(); iter != node->children.end(); iter++) {
                        std::cout << "CHILD " << (*iter)->token << std::endl;
                        if (*iter == tmpNode) {
                            node->children.erase(iter);
                            break;
                        }
                    }
                }
                tmpNode->parent.clear();
                delete tmpNode;
            }
        }

        // 从每个源服务开始删除 然后向上遍历节点 查看节点有没有别的子节点 没有的话代表这节点是属于要删除的路径
        void delete_dService_path2(valueNode *destNode) {
            //std::cout << "YES" << std::endl;
            valueNode *node = NULL;
            for (int i = 0; i < destNode->parent.size(); i++) {

                node = destNode->parent[i];
                delete_dService_node2(node);
            }
            destNode->parent.clear();
        }
*/

/*
        void mergeNode(valueNode *node) {
            // 记录之前出现过的token
            std::unordered_map<std::string, valueNode*> firstTokenNodeSet;
            valueNode *firstStarNode = NULL;
            // 合并节点的遍历
            for (int i = 0; i < node->children.size(); i++) {
                valueNode *candidateNode = node->children[i];
                
                auto iter = firstTokenNodeSet.find(candidateNode->token);
                // 第一次出现该token 记录
                if (iter == firstTokenNodeSet.end()) {
                    firstTokenNodeSet.emplace(candidateNode->token, candidateNode);
                } else if (iter != firstTokenNodeSet.end()) {
                    int temp = candidateNode->tokenCount;
                    iter->second->tokenCount += temp;
                    candidateNode->tokenCount = 0;
                    // 合并节点的子节点到第一个节点上
                    for (int j = 0; j < candidateNode->children.size(); j++) {
                        iter->second->children.push_back(candidateNode->children[j]);
                    }
                }
            }

            // 删除要合并的节点
            for (auto iter = node->children.begin(); iter != node->children.end();) {
                if ((*iter)->tokenCount == 0) {
                    valueNode *deleteNode = *iter;
                    node->children.erase(iter);
                    delete(deleteNode);
                } else {
                    iter++;
                }
            }
            
            // 合并节点后，把更新后的所有节点递归合并节点
            for (int i = 0; i < node->children.size(); i++) {
                mergeNode(node->children[i]);
            }
        }
        */

/*
struct valueNode* navigateLayer(valueNode *node, std::string value, bool isUrl) {
            bool isExistSameToken = false;
            for (int i = 0; i < node->children.size(); i++) {
                if (node->children[i]->token.compare(value) == 0) {
                    isExistSameToken = true;
                    node = node->children[i];
                    break;
                }
            }
            if (isExistSameToken == false) {
                struct valueNode *tNode = new valueNode(value);
                node->children.push_back(tNode);
                node = tNode;
            }

            return node;
         }

        void insert2(std::string sServiceName, std::string dServiceName, std::string httpMethod, 
                    std::string httpUrl, std::string httpVersion) {
            
            std::vector<std::string> urlTokens;
            std::vector<char> splitCharSet = {'/', '&', '?', '='};
            split_url_token(urlTokens, httpUrl, splitCharSet);
            // 增加url的终止节点 避免某些url是某个url的子集 导致分不清api 并且可以接源服务名
            urlTokens.push_back("end");
            int tokensLength = urlTokens.size();

            valueNode *node = this->root;
            valueNode *tNode;
            // 第二层插入 目的服务名
            node = navigateLayer(node, dServiceName, false);
            // 第三层插入 httpVersion
            node = navigateLayer(node, httpVersion, false);
            // 第四层插入 httpMethod
            node = navigateLayer(node, httpMethod, false);

            // 遍历tokens
            for (int i = 0; i < tokensLength; i++) {
                node = navigateLayer(node, urlTokens[i], true);
            }

            // 最后一层插入 源服务名
            node = navigateLayer(node, sServiceName, false);

        }
        // 打印debug用
        void traverse_node2(std::string &bertStr, valueNode *node, int layer, int &count) {
            count++;
            if (node->token.compare("end") == 0) {

                return;
            }
            if (layer > 3) {
                bertStr += node->token + " ";
            }

            
            bertStr += node->token + " ";
            if (node->children.size() == 0) {
                return;
            }
            
            
            for (int i = 0; i < node->children.size(); i++) {
                traverse_node2(bertStr, node->children[i], layer+1, count);
            }
        }

        void traverse_tree2() {
            std::string bertStr;
            int count = 0;
            traverse_node2(bertStr, this->root, 0, count);
            std::cout << "TEST BERT STR: " << bertStr << std::endl;
            std::cout << "TEST NODE COUNT : " << count << std::endl;
        }
*/

/*
void updateDict(std::vector<std::string> templateTokens, std::vector<std::string> tokens) {
            int tokensLength = templateTokens.size();

            for (int i = 0; i < tokensLength; i++) {
                // 两个token不相等
                if (templateTokens[i].compare(tokens[i]) != 0) {
                    auto iter = this->varDict.find(tokens[i]);
                    if (iter != this->varDict.end()) {
                        iter->second++;
                    } else {
                        this->varDict.emplace(tokens[i], 1);
                    }
                }
            } 
        }
        

        // 先存索引 最后再构建string
        void getPositiveDataRecur(std::unordered_map<std::string, int> &wordDict, std::vector<std::string>& pDataFpSet, 
                                valueNode *node, std::vector<int> &pDataFp) {
            
            std::unordered_map<std::string, int>::iterator iter;
            if (node->children.size() == 0) {
                // 构建string
                std::string fpStr;
                for (int i = 0; i < pDataFp.size() - 1; i++) {
                    fpStr += std::to_string(pDataFp[i]) + ':';
                }
                fpStr += std::to_string(pDataFp[pDataFp.size()-1]);
                pDataFpSet.push_back(fpStr);
                return;
            }
            if (node->token.compare("end") == 0) {
                getPositiveDataRecur(wordDict, pDataFpSet, node->children[0], pDataFp);
            } else {
                for (int i = 0; i < node->children.size(); i++) {
                    int index = -1;
                    // 找不到 生成合法数据会遇到找不到的情况吗？
                    if ((iter = wordDict.find(node->token)) == wordDict.end()) {
                        index = wordDict.find(std::to_string('*'))->second;
                    } else {
                        index = iter->second;
                    }
                    pDataFp.push_back(index);
                    getPositiveDataRecur(wordDict, pDataFpSet, node->children[i], pDataFp);
                    pDataFp.pop_back();
                }
            }
        }
*/
/*
void print_tree_struct() {
            std::queue<valueNode*> qNode;
            std::queue<int> qLayer;
            qNode.push(this->root);
            qLayer.push(0);
            int currentPrintLayer = 0;
            valueNode *delimiter = new valueNode("g");
            while (!qNode.empty()) {
                valueNode *node = qNode.front();
                qNode.pop();
                int layer = qLayer.front();
                qLayer.pop();
                if (layer == currentPrintLayer) {
                    if (node->token.compare("g") == 0) {
                        std::cout << "|";
                    } else {
                        std::cout << node->token << " ";
                    }
                } else if (layer > currentPrintLayer) {
                    currentPrintLayer += 1;
                    std::cout << std::endl;
                    std::cout << std::endl;
                    if (node->token.compare("g") == 0) {
                        std::cout << "|";
                    } else {
                        std::cout << node->token << " ";
                    }
                }
                for (int i = 0; i < node->children.size(); i++) {
                    qNode.push(node->children[i]);
                    qLayer.push(layer+1);
                }
                if (node->token.compare("g") != 0) {
                    qNode.push(delimiter);
                    qLayer.push(layer+1);
                }
                
            }    
        }
*/

/*
void stat_node_iter(valueNode *node, std::unordered_set<valueNode*> &statNodeNum) {
    std::cout << "NODE TOKEN : " << node->token << std::endl;
    std::cout << "NODE CHILDREN : ";
    for (int i = 0; i < node->children.size(); i++) {
        std::cout << node->children[i]->token << " ";
    }
    std::cout << std::endl;
    std::unordered_set<valueNode*>::iterator iter;
    iter = statNodeNum.find(node);
    if (iter == statNodeNum.end()) {
        statNodeNum.emplace(node);
    }

    if (node->children.size() == 0) {
        return;
    }
    for (int i = 0; i < node->children.size(); i++) {
        stat_node_iter(node->children[i], statNodeNum);
    }
}

void stat_node() {
    std::unordered_set<valueNode*> statNodeNum;
    stat_node_iter(this->root, statNodeNum);
    std::cout << "NODE COUNT : " << statNodeNum.size() << std::endl;
}
*/

/*
void replace_token_iter(std::vector<std::string>& resultTokens, valueNode *node, int layer, int &resultTokenIndex, int &count) {
            // 按照bertStr构建流程
            if (layer > 2) {
                // 遍历直到目的节点
                if (!dynamic_cast<destServiceNode*>(node)) {
                    count++;
                    // 该token是变量
                    if (resultTokens[resultTokenIndex].compare("1") == 0) {
                        node->token = std::string("*");
                    }
                    resultTokenIndex++;
                } else {
                    return;
                }
            } 
            
            for (int i = 0; i < node->children.size(); i++) {
                replace_token_iter(resultTokens, node->children[i], layer + 1, resultTokenIndex, count);
            }
        }

        void replace_token(std::string predictResult) {
            std::vector<std::string> resultTokens;
            std::vector<char> splitCharSet = {' '};
            split_url_token(resultTokens, predictResult, splitCharSet);
            
            std::cout << "RESULT TOKEN: ";
            for (int i = 0; i < resultTokens.size(); i++) {
                std::cout << resultTokens[i] << " ";
            }
            std::cout << std::endl;
            std::cout << "RESULT TOKEN SIZe: " << resultTokens.size() << std::endl; 
            int resultTokenIndex = 0;
            //test
            int count = 0;
            replace_token_iter(resultTokens, this->root, 0, resultTokenIndex, count);
            std::cout << "replace COUNT : " << count << std::endl;
        }

*/
       
#endif
