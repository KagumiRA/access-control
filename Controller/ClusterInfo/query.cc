#ifndef LIST_POD_H
#define LIST_POD_H

extern "C" {
#include <config/kube_config.h>
#include <api/CoreV1API.h>
#include <api/AppsV1API.h>
#include <stdio.h>
}
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

void split_name(std::vector<std::string> &tokens, std::string name, char terminal) {
    std::string token;
    for (int i = 0; i < name.length(); i++) {
        if (name[i] == terminal) {
            tokens.push_back(token);
            token.clear();
            continue;
        }
        token += name[i];
    }
}

void search_pod_name(apiClient_t *apiClient, std::unordered_map<std::string, std::string> &dict)
{
    // 存储 应该是应付cart cart-db情况
    std::unordered_map<std::string, std::string> podIpPortDict;

    v1_service_list_t *service_list = NULL;
    service_list = CoreV1API_listServiceForAllNamespaces(apiClient, 
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
        );
    
    if (service_list) {
        //std::cout << "Service Info!" << std::endl;
        listEntry_t *listServiceEntry = NULL, *listPortEntry = NULL;
        v1_service_t *service = NULL;
        v1_service_port_t *servicePort = NULL;

        list_ForEach(listServiceEntry, service_list->items) {
            service = (v1_service_t*)listServiceEntry->data;
            std::string serviceIp(service->spec->cluster_ip);
            std::string serviceName(service->metadata->name);

            // 存储ip和名映射 不针对port
            dict.emplace(serviceIp, serviceName);

            podIpPortDict.emplace(serviceName, "");
            //cout << "pod : " << podIp << " | " << podName << endl;
            list_ForEach(listPortEntry, service->spec->ports) {
                servicePort = (v1_service_port_t*)listPortEntry->data;
                //std::cout << "Service Name : " << serviceName << std::endl;
                std::string serviceNodePortStr;
                std::string servicePortStr;
                std::string serviceTargetPortStr;
                // 这个应该是IP:PORT形式 现在先不用
                /*
                if (servicePort->node_port != 0) {
                    serviceNodePortStr = serviceIp + ":" + std::to_string(servicePort->node_port);
                    //std::cout << "Nodeport : " << serviceNodePortStr << std::endl;
                    dict.emplace(serviceNodePortStr, serviceName);
                }

                servicePortStr = serviceIp + ":" + std::to_string(servicePort->port);
                //std::cout << "Port : " << servicePortStr << std::endl;
                dict.emplace(servicePortStr, serviceName);

                if (servicePort->target_port->type == 1) {
                    serviceTargetPortStr = serviceIp + ":" + std::to_string(servicePort->target_port->i);
                    dict.emplace(serviceTargetPortStr, serviceName);
                } else if (servicePort->target_port->type == 2) {
                    // sock-shop 有exporter 未知作用
                    //serviceTargetPortStr = serviceIp + ":" + std::string(servicePort->target_port->s);
                }
                //std::cout << "Targetport : " << serviceTargetPortStr << std::endl;
                */
            }
        }
        v1_service_list_free(service_list);
        service_list = NULL;
    } else {
        printf("Cannot get any svc.\n");
    }


    v1_pod_list_t *pod_list = NULL;
    pod_list = CoreV1API_listPodForAllNamespaces(apiClient,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (pod_list) {
        //std::cout << "Pod Info!" << std::endl;
        listEntry_t *listPodEntry = NULL;
        v1_pod_t *pod = NULL;
        list_ForEach(listPodEntry, pod_list->items) {
            pod = (v1_pod_t*)listPodEntry->data;
            std::string podIp(pod->status->pod_ip);
            // 有pod_ips这种分配几个ip到一个pod的情况，看后面会不会遇到再改
            std::vector<std::string> tokens;
            split_name(tokens, pod->metadata->name, '-');
            // 解决cart-db会先找到cart的问题
            // 因为pod名是cart-kcemekmcekke-efjenej
            // 因此需要以下方式查表，遇到cart-db-dkedemde时，按-为分隔符，逐渐加token
            std::string podName1, podName2;
            std::unordered_map<std::string, std::string>::iterator iter1, iter2;
            podName2 += tokens[0];
            for (int i = 0; i < tokens.size() - 1; i++) {
                if (i != 0) {
                    podName1 += "-" + tokens[i];
                } else {
                    podName1 = tokens[i];
                }
                podName2 += "-" + tokens[i+1];
                iter1 = podIpPortDict.find(podName1);
                iter2 = podIpPortDict.find(podName2);
                // 找到cart和cart-db的情况
                if (iter1 != podIpPortDict.end() && iter2 != podIpPortDict.end()) {
                    continue;
                } else if (iter1 != podIpPortDict.end()) {
                    //std::cout << "Pod : " << podName1 << " | " << podIp << std::endl;
                    podIpPortDict.at(podName1) =  podIp;
                    // Ip 服务名映射
                    dict.emplace(podIp, podName1);
                    break;
                }
            }
            //std::cout << "pod : " << podName1 << " | " << podIp << std::endl;
        }
    }

    v1_deployment_list_t *deployment_list = NULL;
    deployment_list = AppsV1API_listDeploymentForAllNamespaces(apiClient,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (deployment_list) {
        //std::cout << "Deployment Info!" << std::endl;
        listEntry_t *listDeploymentEntry = NULL;
        v1_deployment_t *deployment = NULL;
        list_ForEach(listDeploymentEntry, deployment_list->items) {
            deployment = (v1_deployment_t*)listDeploymentEntry->data;
            std::string deploymentName = deployment->metadata->name;

            // 查看该deployment在不在字典里
            std::unordered_map<std::string, std::string>::iterator iter;
            iter = podIpPortDict.find(deploymentName);
            if (iter != podIpPortDict.end()) {
                listEntry_t *listContainerEntry = NULL;
                v1_container_t *container = NULL;
                list_ForEach(listContainerEntry, deployment->spec->_template->spec->containers) {
                    container = (v1_container_t*)listContainerEntry->data;
                    listEntry_t *listContainerPortEntry = NULL;
                    v1_container_port_t *containerPort = NULL;

                    // 这段针对IP:PORT 现在先不用
                    /*
                    list_ForEach(listContainerPortEntry, container->ports) {
                        containerPort = (v1_container_port_t*)listContainerPortEntry->data;
                        std::string deploymentIpPortStr = iter->second + ":" + std::to_string(containerPort->container_port);
                        dict.emplace(deploymentIpPortStr, deploymentName);

                        // Ip 服务名映射
                        dict.emplace(iter->second, deploymentName);
                        //std::cout << "Doployment : " << deploymentName << " | " << deploymentIpPortStr  << std::endl;
                    }
                    */
                }
            }
            
        }
    } else {
        printf("Cannot get any pod\n");
    }
}

void extract_flannel_proxy_from_config(char *str, std::string &proxyIp) {
    int strLength = strlen(str);
    
    std::string myKey, myValue;
    for (int i = 0; i < strLength; i++) {
        // key
        if (str[i] == '"') {
            i++;
            while (str[i] != '"') {
                myKey += str[i];
                i++;
            }
            i++;
            // value
            while (str[i] != '"') {
                i++;
            }
            i++;
            while (str[i] != '"') {
                myValue += str[i];
                i++;
            }
            i++;
        }

        if (myKey.compare("Network") == 0) {
            int endPosition = myValue.find('/');
            proxyIp = myValue.substr(0, endPosition);
            return;
        }
        myKey.clear(); 
        myValue.clear();
    }
}

void search_flannel_proxy_ip(apiClient_t *apiClient, std::unordered_map<std::string, std::string> &dict) {
    char *config_map_name = "kube-flannel-cfg";
    char *namespace_ = "kube-flannel";
    char *api_version = "v1";
    char *kind = "ConfigMap";

    v1_config_map_list_t *config_map_list = CoreV1API_listNamespacedConfigMap(
        apiClient,
        namespace_,   // char *namespace
        "true",   // char *pretty
        NULL, // int *allowWatchBookmarks
        NULL, // char *_continue
        NULL, // char *fieldSelector
        NULL, // char *labelSelector
        NULL, // int *limit
        NULL, // char *resourceVersion
        NULL, // char *resourceVersionMatch
        NULL, // sendInitialEvents
        NULL, // int *timeoutSeconds
        NULL  //int *watch
    );

    //printf("The return code of list ConfigMap = %ld\n", apiClient->response_code);

    if (config_map_list && config_map_list->items) {
        listEntry_t *config_map_list_entry = NULL;
        v1_config_map_t *config_map = NULL;
        list_ForEach(config_map_list_entry, config_map_list->items) {
            config_map = config_map_list_entry->data;
            //printf("\tThe config map name: %s\n", config_map->metadata->name);

            listEntry_t *data_entry = NULL;
            keyValuePair_t *pair = NULL;
            list_ForEach(data_entry, config_map->data) {
                pair = data_entry->data;
                if (strcmp(pair->key, "net-conf.json") == 0) {
                    std::string proxyIp;
                    extract_flannel_proxy_from_config(pair->value, proxyIp);
                    std::string flannelStr("flannel-proxy");
                    dict.emplace(proxyIp, flannelStr);
                    //std::cout << "proxyIp : " << proxyIp << std::endl;
                }
            }
        }
        v1_config_map_list_free(config_map_list);
        config_map_list = NULL;
    }
}

// 构建ip 服务映射
void build_ip_service_mapping(std::unordered_map<std::string, std::string> &dict)
{
    char *basePath = NULL;
    sslConfig_t *sslConfig = NULL;
    list_t *apiKeys = NULL;
    //build_config(basePath, sslConfig, apiKeys, apiClient);
    int rc = load_kube_config(&basePath, &sslConfig, &apiKeys, NULL);   // NULL means loading configuration from $HOME/.kube/config 
    if (rc != 0) {
        printf("Cannot load kubernetes configuration.\n");
        return;
    }
    apiClient_t *apiClient = apiClient_create_with_base_path(basePath, sslConfig, apiKeys);
    if (!apiClient) {
        printf("Cannot create a kubernetes client.\n");
        return;
    }
    
    // dict 有所有ip:port对应的服务名 ip:port作为key查询
    // 先找flannel ip:port
    search_flannel_proxy_ip(apiClient, dict);
    search_pod_name(apiClient, dict);

    apiClient_free(apiClient);
    apiClient = NULL;
    free_client_config(basePath, sslConfig, apiKeys);
    basePath = NULL;
    sslConfig = NULL;
    apiKeys = NULL;
    apiClient_unsetupGlobalEnv();

    return;
}

void askAPIServer_destService_node(apiClient_t *apiClient, std::unordered_map<std::string, std::string> &podNodeIpMap, char *namespace_) {
    //char *namespace_ = "sock-shop";

    // 存服务名 用来处理carts carts-db
    std::unordered_set<std::string> serviceNameSet;
    v1_service_list_t *service_list = CoreV1API_listNamespacedService(
        apiClient,
        namespace_,
        "true",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (service_list) {
        listEntry_t *serviceEntry = NULL;
        v1_service_t *service = NULL;
        list_ForEach(serviceEntry, service_list->items) {
            service = (v1_service_t*)serviceEntry->data;
            //std::string serviceName(service->metadata->name);
            serviceNameSet.emplace(std::string(service->metadata->name));
        }
    }

    // 存节点名和节点IP的映射 用来给pod映射
    // serviceNameIPMap | worker115 10.10.3.115
    std::unordered_map<std::string, std::string> serviceNameIPMap;
    v1_node_list_t *node_list = CoreV1API_listNode(
        apiClient,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (node_list) {
        listEntry_t *nodeEntry = NULL;
        v1_node_t *node = NULL;

        list_ForEach(nodeEntry, node_list->items) {
            node = (v1_node_t*)nodeEntry->data;
            std::string nodeName(node->metadata->name);
            listEntry_t *nodeIpEntry = NULL;
            v1_node_address_t *nodeIp = NULL;
            list_ForEach(nodeIpEntry, node->status->addresses) {
                nodeIp = (v1_node_address_t*)nodeIpEntry->data;
                std::string nodeIpName(nodeIp->address);
                if (strcmp(nodeIp->type, "InternalIP") == 0) {
                    // 假设目前只有1个node只有1个nodeIp
                    serviceNameIPMap.emplace(nodeName, nodeIpName);
                }
            }
        }
    }


    v1_pod_list_t *pod_list = CoreV1API_listNamespacedPod(
        apiClient,
        namespace_,
        "true",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (pod_list) {
        listEntry_t *podEntry = NULL;
        v1_pod_t *pod = NULL;

        list_ForEach(podEntry, pod_list->items) {
            pod = (v1_pod_t*)podEntry->data;
            std::string podNodeName(pod->spec->node_name);
            std::vector<std::string> tokens;
            split_name(tokens, pod->metadata->name, '-');
            std::string podName1, podName2;
            std::unordered_set<std::string>::iterator iter1, iter2;
            podName2 += tokens[0];
            //std::cout << "NODE1: " << pod->metadata->name << " " << tokens.size() << std::endl;
            for (int i = 0; i < tokens.size() - 1; i++) {
                if (i != 0) {
                    podName1 += "-" + tokens[i];
                } else {
                    podName1 = tokens[i];
                }
                podName2 += "-" + tokens[i+1];
                iter1 = serviceNameSet.find(podName1);
                iter2 = serviceNameSet.find(podName2);
                
                if (iter1 != serviceNameSet.end() && iter2 != serviceNameSet.end()) {
                    continue;
                } else if (iter1 != serviceNameSet.end()) {
                    //std::cout << "NODE2: " << podName1 << std::endl;
                    // Ip 服务名映射
                    std::unordered_map<std::string, std::string>::iterator iter;
                    iter = serviceNameIPMap.find(podNodeName);
                    if (iter != serviceNameIPMap.end()) {
                        podNodeIpMap.emplace(podName1, iter->second);
                    }
                    break;
                }
            }
        }
    }
}

// 控制节点发送规则到工作节点 查看哪个工作节点有哪个目的服务
void find_pod_nodeIp_mapping(std::unordered_map<std::string, std::string> &podNodeIpMap, char *namespace_) {
    char *basePath = NULL;
    sslConfig_t *sslConfig = NULL;
    list_t *apiKeys = NULL;
    int rc = load_kube_config(&basePath, &sslConfig, &apiKeys, NULL);   // NULL means loading configuration from $HOME/.kube/config 
    if (rc != 0) {
        printf("Cannot load kubernetes configuration.\n");
        return;
    }
    apiClient_t *apiClient = apiClient_create_with_base_path(basePath, sslConfig, apiKeys);
    if (!apiClient) {
        printf("Cannot create a kubernetes client.\n");
        return;
    }


    askAPIServer_destService_node(apiClient, podNodeIpMap, namespace_);


    apiClient_free(apiClient);
    apiClient = NULL;
    free_client_config(basePath, sslConfig, apiKeys);
    basePath = NULL;
    sslConfig = NULL;
    apiKeys = NULL;
    apiClient_unsetupGlobalEnv();

    return;
}

void askAPIServer_all_node(apiClient_t *apiClient, std::unordered_set<std::string> &nodeSet) {
    // 存节点名和节点IP的映射 用来给pod映射
    v1_node_list_t *node_list = CoreV1API_listNode(
        apiClient,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (node_list) {
        listEntry_t *nodeEntry = NULL;
        v1_node_t *node = NULL;

        list_ForEach(nodeEntry, node_list->items) {
            node = (v1_node_t*)nodeEntry->data;
            listEntry_t *nodeIpEntry = NULL;
            v1_node_address_t *nodeIp = NULL;
            list_ForEach(nodeIpEntry, node->status->addresses) {
                nodeIp = (v1_node_address_t*)nodeIpEntry->data;
                if (strcmp(nodeIp->type, "InternalIP") == 0) {
                    // 假设目前只有1个node只有1个nodeIp
                    nodeSet.emplace(std::string(nodeIp->address));
                }
            }
        }
    }
}

void find_all_node(std::unordered_set<std::string> &nodeSet) {
    char *basePath = NULL;
    sslConfig_t *sslConfig = NULL;
    list_t *apiKeys = NULL;
    int rc = load_kube_config(&basePath, &sslConfig, &apiKeys, NULL);   // NULL means loading configuration from $HOME/.kube/config 
    if (rc != 0) {
        printf("Cannot load kubernetes configuration.\n");
        return;
    }
    apiClient_t *apiClient = apiClient_create_with_base_path(basePath, sslConfig, apiKeys);
    if (!apiClient) {
        printf("Cannot create a kubernetes client.\n");
        return;
    }

    askAPIServer_all_node(apiClient, nodeSet);

    apiClient_free(apiClient);
    apiClient = NULL;
    free_client_config(basePath, sslConfig, apiKeys);
    basePath = NULL;
    sslConfig = NULL;
    apiKeys = NULL;
    apiClient_unsetupGlobalEnv();

    return;
}
#endif
