// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"
#include <winsock2.h>

void ThreadMessageHandler2(void* parg);
void ThreadSocketHandler2(void* parg);
void ThreadOpenConnections2(void* parg);

// 校验 socket 句柄是否有效
// 参数：sock - 待校验的 SOCKET 句柄
// 返回值：true = 有效，false = 无效
bool isValidSocket(SOCKET sock);








//
// Global state variables
//
bool fClient = false;
uint64 nLocalServices = (fClient ? 0 : NODE_NETWORK);
CAddress addrLocalHost(0, DEFAULT_PORT, nLocalServices);
CNode nodeLocalHost(INVALID_SOCKET, CAddress("127.0.0.1", nLocalServices));
CNode* pnodeLocalHost = &nodeLocalHost;
bool fShutdown = false;
uint64 nLocalHostNonce = 0;
set<unsigned int> setLocalHostIPv4;
std::array<bool, 10> vfThreadRunning;
vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<vector<unsigned char>, CAddress> mapAddresses;
CCriticalSection cs_mapAddresses;
map<CInv, CDataStream> mapRelay;
deque<pair<int64, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
map<CInv, int64> mapAlreadyAskedFor;




CAddress addrProxy;

bool ConnectSocket(const CAddress& addrConnect, SOCKET& hSocketRet)
{
    //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    hSocketRet = INVALID_SOCKET;

    SOCKET hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hSocket == INVALID_SOCKET)
        return false;

    //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    bool fRoutable = !(addrConnect.GetByte(3) == 10 || (addrConnect.GetByte(3) == 192 && addrConnect.GetByte(2) == 168));
    bool fProxy = (addrProxy.ip && fRoutable);
    struct sockaddr_in sockaddr = (fProxy ? addrProxy.GetSockAddr() : addrConnect.GetSockAddr());


    //printf("Tring connect to %s. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",addrConnect.ToStringIPPort().c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    if (connect(hSocket, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) == SOCKET_ERROR)
    {
        printf("Connect to %s failed(connect() return SOCKET_ERROR.errcode:%d). Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",addrConnect.ToStringIPPort().c_str(),WSAGetLastError() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        closesocket(hSocket);
        return false;
    }

    //printf("Connect to %s done. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",addrConnect.ToStringIPPort().c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    if (fProxy)
    {
        printf("Proxy connecting to %s\n", addrConnect.ToString().c_str());
        char pszSocks4IP[] = "\4\1\0\0\0\0\0\0user";
        memcpy(pszSocks4IP + 2, &addrConnect.port, 2);
        memcpy(pszSocks4IP + 4, &addrConnect.ip, 4);
        char* pszSocks4 = pszSocks4IP;
        int nSize = sizeof(pszSocks4IP);

        int ret = send(hSocket, pszSocks4, nSize, 0);
        if (ret != nSize)
        {
            closesocket(hSocket);
            return error("Error sending to proxy\n");
        }
        char pchRet[8];
        if (recv(hSocket, pchRet, 8, 0) != 8)
        {
            closesocket(hSocket);
            return error("Error reading proxy response\n");
        }
        if (pchRet[1] != 0x5a)
        {
            closesocket(hSocket);
            return error("Proxy returned error %d\n", pchRet[1]);
        }
        printf("Proxy connection established %s\n", addrConnect.ToString().c_str());
    }

    hSocketRet = hSocket;
    //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    return true;
}



bool GetMyExternalIP2(const CAddress& addrConnect, const char* pszGet, const char* pszKeyword, unsigned int& ipRet)
{
    SOCKET hSocket;
    if (!ConnectSocket(addrConnect, hSocket))
        return error("GetMyExternalIP() : connection to %s failed\n", addrConnect.ToString().c_str());

    send(hSocket, pszGet, strlen(pszGet), 0);

    string strLine;
    while (RecvLine(hSocket, strLine))
    {
        if (strLine.empty())
        {
            loop
            {
                if (!RecvLine(hSocket, strLine))
                {
                    closesocket(hSocket);
                    return false;
                }
                if (strLine.find(pszKeyword) != -1)
                {
                    strLine = strLine.substr(strLine.find(pszKeyword) + strlen(pszKeyword));
                    break;
                }
            }
            closesocket(hSocket);
            if (strLine.find("<"))
                strLine = strLine.substr(0, strLine.find("<"));
            strLine = strLine.substr(strspn(strLine.c_str(), " \t\n\r"));
            strLine = wxString(strLine).Trim();
            CAddress addr(strLine.c_str(),NODE_NETWORK);
            printf("GetMyExternalIP() received [%s] %s\n", strLine.c_str(), addr.ToString().c_str());
            if (addr.ip == 0 || !addr.IsRoutable())
                return false;
            ipRet = addr.ip;
            return true;
        }
    }
    closesocket(hSocket);
    return error("GetMyExternalIP() : connection closed\n");
}


bool GetMyExternalIP(unsigned int& ipRet)
{
    CAddress addrConnect;
    char* pszGet;
    char* pszKeyword;

    for (int nLookup = 1; nLookup <= 1; nLookup++)
    for (int nHost = 1; nHost <= 2; nHost++)
    {
        if (nHost == 1)
        {
            addrConnect = CAddress("70.86.96.218:80"); // www.ipaddressworld.com

            if (nLookup == 1)
            {
                struct hostent* phostent = gethostbyname("www.ipaddressworld.com");
                addrConnect = CAddress(*(u_long*)phostent->h_addr_list[0], htons(80));
            }

            pszGet = "GET /ip.php HTTP/1.1\r\n"
                     "Host: www.ipaddressworld.com\r\n"
                     "User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1)\r\n"
                     "Connection: close\r\n"
                     "\r\n";

            //pszKeyword = "IP:";
            pszKeyword = "cf-footer-ip\"\>";
        }
        else if (nHost == 2)
        {
            addrConnect = CAddress("208.78.68.70:80"); // checkip.dyndns.org

            if (nLookup == 1)
            {
                struct hostent* phostent = gethostbyname("checkip.dyndns.org");
                addrConnect = CAddress(*(u_long*)phostent->h_addr_list[0], htons(80));
            }

            pszGet = "GET / HTTP/1.1\r\n"
                     "Host: checkip.dyndns.org\r\n"
                     "User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 5.1)\r\n"
                     "Connection: close\r\n"
                     "\r\n";

            pszKeyword = "Address:";
        }

        if (GetMyExternalIP2(addrConnect, pszGet, pszKeyword, ipRet))
            return true;
    }

    return false;
}





bool AddAddress(CAddrDB& addrdb, const CAddress& addr)
{
    //if (!addr.IsRoutable())
    //    return false;
    //if (addr.ip == addrLocalHost.ip)
    //    return false;
    if (!addr.IsIPv4())
    {
        printf("AddAddress():Address[%s] is not IPv4 will be ignore.\n",addr.ToString().c_str());
        return false;
    }
	if (addr.IsLocalListenAddr())
	{
		printf("AddAddress():%s is local listening address,skip it.\n"
			,addr.ToStringIPPort().c_str());
		return false;
	}

    CRITICAL_BLOCK(cs_mapAddresses)
    {
        map<vector<unsigned char>, CAddress>::iterator it = mapAddresses.find(addr.GetKey());
        if (it == mapAddresses.end())
        {
            // New address
            mapAddresses.insert(make_pair(addr.GetKey(), addr));
            addrdb.WriteAddress(addr);
			printf("AddAddress()[add new]:%s\n",addr.ToString().c_str());
            return true;
        }
        else
        {
            CAddress& addrFound = (*it).second;
            if ((addrFound.nServices | addr.nServices) != addrFound.nServices)
            {
                // Services have been added
                addrFound.nServices |= addr.nServices;
                addrdb.WriteAddress(addrFound);
                printf("AddAddress()[update]:%s\n",addr.ToString().c_str());
                return true;
            }
            else
                printf("AddAddress()[nothing]:%s\n",addr.ToString().c_str());
        }
    }
    return false;
}





void AbandonRequests(void (*fn)(void*, CDataStream&), void* param1)
{
    // If the dialog might get closed before the reply comes back,
    // call this in the destructor so it doesn't get called after it's deleted.
    CRITICAL_BLOCK(cs_vNodes)
    {
        foreach(CNode* pnode, vNodes)
        {
            CRITICAL_BLOCK(pnode->cs_mapRequests)
            {
                for (map<uint256, CRequestTracker>::iterator mi = pnode->mapRequests.begin(); mi != pnode->mapRequests.end();)
                {
                    CRequestTracker& tracker = (*mi).second;
                    if (tracker.fn == fn && tracker.param1 == param1)
                        pnode->mapRequests.erase(mi++);
                    else
                        mi++;
                }
            }
        }
    }
}







//
// Subscription methods for the broadcast and subscription system.
// Channel numbers are message numbers, i.e. MSG_TABLE and MSG_PRODUCT.
//
// The subscription system uses a meet-in-the-middle strategy.
// With 100,000 nodes, if senders broadcast to 1000 random nodes and receivers
// subscribe to 1000 random nodes, 99.995% (1 - 0.99^1000) of messages will get through.
//

bool AnySubscribed(unsigned int nChannel)
{
    if (pnodeLocalHost->IsSubscribed(nChannel))
        return true;
    CRITICAL_BLOCK(cs_vNodes)
        foreach(CNode* pnode, vNodes)
            if (pnode->IsSubscribed(nChannel))
                return true;
    return false;
}

bool CNode::IsSubscribed(unsigned int nChannel)
{
    if (nChannel >= vfSubscribe.size())
        return false;
    return vfSubscribe[nChannel];
}

void CNode::Subscribe(unsigned int nChannel, unsigned int nHops)
{
    if (nChannel >= vfSubscribe.size())
        return;

    if (!AnySubscribed(nChannel))
    {
        // Relay subscribe
        CRITICAL_BLOCK(cs_vNodes)
            foreach(CNode* pnode, vNodes)
                if (pnode != this)
                    pnode->PushMessage("subscribe", nChannel, nHops);
    }

    vfSubscribe[nChannel] = true;
}

void CNode::CancelSubscribe(unsigned int nChannel)
{
    if (nChannel >= vfSubscribe.size())
        return;

    // Prevent from relaying cancel if wasn't subscribed
    if (!vfSubscribe[nChannel])
        return;
    vfSubscribe[nChannel] = false;

    if (!AnySubscribed(nChannel))
    {
        // Relay subscription cancel
        CRITICAL_BLOCK(cs_vNodes)
            foreach(CNode* pnode, vNodes)
                if (pnode != this)
                    pnode->PushMessage("sub-cancel", nChannel);

        // Clear memory, no longer subscribed
        if (nChannel == MSG_PRODUCT)
            CRITICAL_BLOCK(cs_mapProducts)
                mapProducts.clear();
    }
}









CNode* FindNode(unsigned int ip)
{
    CRITICAL_BLOCK(cs_vNodes)
    {
        foreach(CNode* pnode, vNodes)
            if (pnode->addr.ip == ip)
                return (pnode);
    }
    return NULL;
}

CNode* FindNode(CAddress addr)
{
    CRITICAL_BLOCK(cs_vNodes)
    {
        foreach(CNode* pnode, vNodes)
            if (pnode->addr == addr)
                return (pnode);
    }
    return NULL;
}

CNode* ConnectNode(CAddress addrConnect, int64 nTimeout)
{
    if (addrConnect.ip == addrLocalHost.ip)
        return NULL;

    // Look for an existing connection
    CNode* pnode = FindNode(addrConnect.ip);
    if (pnode)
    {
        if (nTimeout != 0)
            pnode->AddRef(nTimeout);
        else
            pnode->AddRef();
        return pnode;
    }

    /// debug print
    printf("trying %s\n", addrConnect.ToStringIPPort().c_str());

    // Connect
    SOCKET hSocket;
    if (ConnectSocket(addrConnect, hSocket))
    {
        /// debug print
        printf("connected %s\n", addrConnect.ToStringIPPort().c_str());

        // Set to nonblocking
        u_long nOne = 1;
        if (ioctlsocket(hSocket, FIONBIO, &nOne) == SOCKET_ERROR)
            printf("ConnectSocket() : ioctlsocket nonblocking setting failed, error %d\n", WSAGetLastError());

        // Add node
        CNode* pnode = new CNode(hSocket, addrConnect, false);
        if (nTimeout != 0)
            pnode->AddRef(nTimeout);
        else
            pnode->AddRef();
        CRITICAL_BLOCK(cs_vNodes)
            vNodes.push_back(pnode);

        CRITICAL_BLOCK(cs_mapAddresses)
            mapAddresses[addrConnect.GetKey()].nLastFailed = 0;
        return pnode;
    }
    else
    {
        CRITICAL_BLOCK(cs_mapAddresses)
            mapAddresses[addrConnect.GetKey()].nLastFailed = GetTime();
        return NULL;
    }
}

void CNode::Disconnect()
{
    printf("disconnecting node %s\n", addr.ToString().c_str());

    if (hSocket != INVALID_SOCKET)
    {
        closesocket(hSocket);
        hSocket = INVALID_SOCKET;
    }

    // All of a nodes broadcasts and subscriptions are automatically torn down
    // when it goes down, so a node has to stay up to keep its broadcast going.

    CRITICAL_BLOCK(cs_mapProducts)
        for (map<uint256, CProduct>::iterator mi = mapProducts.begin(); mi != mapProducts.end();)
            AdvertRemoveSource(this, MSG_PRODUCT, 0, (*(mi++)).second);

    // Cancel subscriptions
    for (unsigned int nChannel = 0; nChannel < vfSubscribe.size(); nChannel++)
        if (vfSubscribe[nChannel])
            CancelSubscribe(nChannel);
}







bool isValidSocket(SOCKET sock)
{
    // 1. 基础范围校验（过滤明显非法的句柄）
    // Windows 中 SOCKET 是 unsigned int 类型，INVALID_SOCKET 定义为 0xFFFFFFFF（无效句柄）
    // 正常 socket 句柄通常是 1~65535（默认最大句柄数，可通过 WSADATA.wMaxSockets 获取）
    if (sock == INVALID_SOCKET || sock == 0)
    {
        return false;
    }

    // 2. 用 getsockopt 校验句柄是否为 Winsock 合法 socket（核心步骤）
    // 原理：向 Winsock 内核查询 socket 的 SO_TYPE 选项（获取 socket 类型，如 SOCK_STREAM/SOCK_DGRAM）
    // 若句柄无效，getsockopt 会返回 SOCKET_ERROR，且 WSAGetLastError() 返回 WSAENOTSOCK
    int sockType = 0;
    int optLen = sizeof(sockType);
    int ret = getsockopt(
        sock,        // 待校验的 socket 句柄
        SOL_SOCKET,  // 选项级别（SOL_SOCKET = 通用 socket 选项）
        SO_TYPE,     // 选项名（SO_TYPE = 获取 socket 类型）
        (char*)&sockType,  // 输出：socket 类型
        &optLen      // 输入/输出：选项值长度
    );

    if (ret == SOCKET_ERROR)
    {
        // 获取错误码，进一步确认是否为「非 socket 句柄」
        int err = WSAGetLastError();
        // WSAENOTSOCK = 句柄不是有效的 Winsock socket（最常见的无效场景）
        if (err == WSAENOTSOCK)
        {
            return false;
        }
        // 其他错误（如 WSAENETDOWN 网络中断、WSAEINPROGRESS 正在处理异步操作）：
        // 此时句柄本身是有效的，只是暂时无法操作，返回 true
    }

    // 3. （可选）校验 socket 是否处于「已连接/可操作」状态（按需添加）
    // 注意：此步骤会过滤掉「已创建但未连接」的 socket（如服务器监听 socket），需根据业务场景决定是否保留
    /*
    struct sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getpeername(sock, (struct sockaddr*)&addr, &addrLen) == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        // WSAENOTCONN = 未连接（如客户端未调用 connect，或服务器监听 socket 无连接）
        if (err == WSAENOTCONN)
        {
            // 若业务需要监听「未连接的 socket」（如服务器监听 socket），则返回 true；否则返回 false
            return false;
        }
    }
    */

    // 所有校验通过，句柄有效
    return true;
}





void ThreadSocketHandler(void* parg)
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadSocketHandler(parg));

    loop
    {
        vfThreadRunning[0] = true;
        CheckForShutdown(0);
        try
        {
            ThreadSocketHandler2(parg);
        }
        CATCH_PRINT_EXCEPTION("ThreadSocketHandler()")
        vfThreadRunning[0] = false;
        Sleep(5000);
    }
}

void ThreadSocketHandler2(void* parg)
{
    printf("ThreadSocketHandler started\n");
    //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    SOCKET hListenSocket = *(SOCKET*)parg;
    list<CNode*> vNodesDisconnected;
    int nPrevNodeCount = 0;

    loop
    {
        //
        // Disconnect nodes
        //
        CRITICAL_BLOCK(cs_vNodes)
        {
            // Disconnect unused nodes
            vector<CNode*> vNodesCopy = vNodes;
            foreach(CNode* pnode, vNodesCopy)
            {
                if (pnode->ReadyToDisconnect() && pnode->vRecv.empty() && pnode->vSend.empty())
                {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());
                    pnode->Disconnect();

                    // hold in disconnected pool until all refs are released
                    pnode->nReleaseTime = max(pnode->nReleaseTime, GetTime() + 5 * 60);
                    if (pnode->fNetworkNode)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
                }
            }

            // Delete disconnected nodes
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            foreach(CNode* pnode, vNodesDisconnectedCopy)
            {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0)
                {
                    bool fDelete = false;
                    TRY_CRITICAL_BLOCK(pnode->cs_vSend)
                     TRY_CRITICAL_BLOCK(pnode->cs_vRecv)
                      TRY_CRITICAL_BLOCK(pnode->cs_mapRequests)
                       TRY_CRITICAL_BLOCK(pnode->cs_inventory)
                        fDelete = true;
                    if (fDelete)
                    {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        if (vNodes.size() != nPrevNodeCount)
        {
            nPrevNodeCount = vNodes.size();
            //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            MainFrameRepaint();
        }


        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        struct fd_set fdsetRecv;
        struct fd_set fdsetSend;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        SOCKET hSocketMax = 0;
        FD_SET(hListenSocket, &fdsetRecv);
        hSocketMax = max(hSocketMax, hListenSocket);
        CRITICAL_BLOCK(cs_vNodes)
        {
            foreach(CNode* pnode, vNodes)
            {
                if (isValidSocket(pnode->hSocket))
                {
                    FD_SET(pnode->hSocket, &fdsetRecv);
                    hSocketMax = max(hSocketMax, pnode->hSocket);
                    TRY_CRITICAL_BLOCK(pnode->cs_vSend)
                        if (!pnode->vSend.empty())
                            FD_SET(pnode->hSocket, &fdsetSend);
                }
            }
        }

        vfThreadRunning[0] = false;
        int nSelect = select(hSocketMax + 1, &fdsetRecv, &fdsetSend, NULL, &timeout);
        vfThreadRunning[0] = true;
        CheckForShutdown(0);
        if (nSelect == SOCKET_ERROR)
        {
            int nErr = WSAGetLastError();

			char errMsg[512] = {};
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, nErr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), errMsg, sizeof(errMsg), NULL);
			printf("select failed: %d - %s\n", nErr, errMsg);
            for (int i = 0; i <= hSocketMax; i++)
            {
				if (isValidSocket(i))
				{
					FD_SET(i, &fdsetRecv);
					FD_SET(i, &fdsetSend);

					if (!(FD_ISSET(i, &fdsetRecv)))
						printf("FD_SET recv failed for socket %d\n", i);
					if (!(FD_ISSET(i, &fdsetSend)))
						printf("FD_SET send failed for socket %d\n", i);
				}
            }
			switch (nErr)
			{
				case WSAEINVAL:
				case WSAENOBUFS:
					// 非致命错误，按原逻辑休眠重试
					Sleep(timeout.tv_usec/1000);
					break;
				case WSAENOTSOCK:
				case WSAEADDRINUSE:
					// 致命错误，清理资源后退出线程
					//CleanupSockets(); // 自定义清理函数（关闭所有 socket）
					return; // 退出线程
				default:
					// 其他错误，休眠重试
					Sleep(timeout.tv_usec/1000);
					break;
			}
        }
        RandAddSeed();

        //// debug print
        //foreach(CNode* pnode, vNodes)
        //{
        //    printf("vRecv = %-5d ", pnode->vRecv.size());
        //    printf("vSend = %-5d    ", pnode->vSend.size());
        //}
        //printf("\n");


        //
        // Accept new connections
        //
        if (FD_ISSET(hListenSocket, &fdsetRecv))
        {
            struct sockaddr_in sockaddr;
            int len = sizeof(sockaddr);
            SOCKET hSocket = accept(hListenSocket, (struct sockaddr*)&sockaddr, &len);
            CAddress addr(sockaddr);
            if (hSocket == INVALID_SOCKET)
            {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                    printf("ERROR ThreadSocketHandler accept failed: %d\n", WSAGetLastError());
            }
            else
            {
                printf("accepted connection from %s\n", addr.ToString().c_str());
                CNode* pnode = new CNode(hSocket, addr, true);
                pnode->AddRef();
                CRITICAL_BLOCK(cs_vNodes)
                    vNodes.push_back(pnode);
            }
        }


        //
        // Service each socket
        //
        vector<CNode*> vNodesCopy;
        CRITICAL_BLOCK(cs_vNodes)
            vNodesCopy = vNodes;
        foreach(CNode* pnode, vNodesCopy)
        {
            CheckForShutdown(0);
            SOCKET hSocket = pnode->hSocket;

            //
            // Receive
            //
            if (FD_ISSET(hSocket, &fdsetRecv))
            {
                TRY_CRITICAL_BLOCK(pnode->cs_vRecv)
                {
                    CDataStream& vRecv = pnode->vRecv;
                    unsigned int nPos = vRecv.size();

                    // typical socket buffer is 8K-64K
                    const unsigned int nBufSize = 0x10000;
                    vRecv.resize(nPos + nBufSize);
                    int nBytes = recv(hSocket, &vRecv[nPos], nBufSize, 0);
                    vRecv.resize(nPos + max(nBytes, 0));
                    if (nBytes == 0)
                    {
                        // socket closed gracefully
                        if (!pnode->fDisconnect)
                            printf("recv: socket closed\n");
                        pnode->fDisconnect = true;
                    }
                    else if (nBytes < 0)
                    {
                        // socket error
                        int nErr = WSAGetLastError();
                        if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                        {
                            if (!pnode->fDisconnect)
                                printf("recv failed: %d\n", nErr);
                            pnode->fDisconnect = true;
                        }
                    }
                }
            }

            //
            // Send
            //
            if (FD_ISSET(hSocket, &fdsetSend))
            {
                TRY_CRITICAL_BLOCK(pnode->cs_vSend)
                {
                    CDataStream& vSend = pnode->vSend;
                    if (!vSend.empty())
                    {
                        int nBytes = send(hSocket, &vSend[0], vSend.size(), 0);
                        if (nBytes > 0)
                        {
                            vSend.erase(vSend.begin(), vSend.begin() + nBytes);
                        }
                        else if (nBytes == 0)
                        {
                            if (pnode->ReadyToDisconnect())
                                pnode->vSend.clear();
                        }
                        else
                        {
                            printf("send error %d\n", nBytes);
                            if (pnode->ReadyToDisconnect())
                                pnode->vSend.clear();
                        }
                    }
                }
            }
        }


        Sleep(10);
        //printf("Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    }
}










void ThreadOpenConnections(void* parg)
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadOpenConnections(parg));

    loop
    {
        vfThreadRunning[1] = true;
        CheckForShutdown(1);
        try
        {
            ThreadOpenConnections2(parg);
        }
        CATCH_PRINT_EXCEPTION("ThreadOpenConnections()")
        vfThreadRunning[1] = false;
        Sleep(5000);
    }
}

void ThreadOpenConnections2(void* parg)
{
    printf("ThreadOpenConnections started\n");

    // Initiate network connections
    const int nMaxConnections = 15;
    loop
    {
        // Wait
        vfThreadRunning[1] = false;
        Sleep(500);
        while (vNodes.size() >= nMaxConnections || vNodes.size() >= mapAddresses.size())
        {
            CheckForShutdown(1);
            Sleep(2000);
        }
        vfThreadRunning[1] = true;
        CheckForShutdown(1);


        // Make a list of unique class C's
        unsigned char pchIPCMask[4] = { 0xff, 0xff, 0xff, 0x00 };
        unsigned int nIPCMask = *(unsigned int*)pchIPCMask;
        vector<unsigned int> vIPC;
        CRITICAL_BLOCK(cs_mapAddresses)
        {
            vIPC.reserve(mapAddresses.size());
            unsigned int nPrev = 0;
            foreach(const PAIRTYPE(vector<unsigned char>, CAddress)& item, mapAddresses)
            {
                const CAddress& addr = item.second;
                if (!addr.IsIPv4())
                    continue;

                // Taking advantage of mapAddresses being in sorted order,
                // with IPs of the same class C grouped together.
                unsigned int ipC = addr.ip & nIPCMask;
                if (ipC != nPrev)
                    vIPC.push_back(nPrev = ipC);
            }
        }

        //
        // The IP selection process is designed to limit vulnerability to address flooding.
        // Any class C (a.b.c.?) has an equal chance of being chosen, then an IP is
        // chosen within the class C.  An attacker may be able to allocate many IPs, but
        // they would normally be concentrated in blocks of class C's.  They can hog the
        // attention within their class C, but not the whole IP address space overall.
        // A lone node in a class C will get as much attention as someone holding all 255
        // IPs in another class C.
        //
        bool fSuccess = false;
        int nLimit = vIPC.size();
        while (!fSuccess && nLimit-- > 0)
        {
            // Choose a random class C
            unsigned int ipC = vIPC[GetRand(vIPC.size())];

            // Organize all addresses in the class C by IP
            map<unsigned int, vector<CAddress> > mapIP;
            CRITICAL_BLOCK(cs_mapAddresses)
            {
                unsigned int nDelay = ((30 * 60) << vNodes.size());
                if (nDelay > 8 * 60 * 60)
                    nDelay = 8 * 60 * 60;
                for (map<vector<unsigned char>, CAddress>::iterator mi = mapAddresses.lower_bound(CAddress(ipC, 0).GetKey());
                     mi != mapAddresses.upper_bound(CAddress(ipC | ~nIPCMask, 0xffff).GetKey());
                     ++mi)
                {
                    const CAddress& addr = (*mi).second;
                    unsigned int nRandomizer = (addr.nLastFailed * addr.ip * 7777U) % 20000;
                    if (GetTime() - addr.nLastFailed > nDelay * nRandomizer / 10000)
                        mapIP[addr.ip].push_back(addr);
                }
            }
            if (mapIP.empty())
                break;

            // Choose a random IP in the class C
            map<unsigned int, vector<CAddress> >::iterator mi = mapIP.begin();
            advance(mi, GetRand(mapIP.size()));

            // Once we've chosen an IP, we'll try every given port before moving on
            foreach(const CAddress& addrConnect, (*mi).second)
            {
                CheckForShutdown(1);
                if (addrConnect.ip == addrLocalHost.ip || !addrConnect.IsIPv4() || FindNode(addrConnect.ip))
                    continue;

                vfThreadRunning[1] = false;
                CNode* pnode = ConnectNode(addrConnect);
                vfThreadRunning[1] = true;
                CheckForShutdown(1);
                if (!pnode)
                    continue;
                pnode->fNetworkNode = true;

                 // Advertise our address
                vector<CAddress> vAddrToSend;
                std::string strGoldenIP = "106.12.23.137";
                unsigned int uintGolderIP = inet_addr(strGoldenIP.c_str()) ;
                if (pnode->addr.ip != uintGolderIP)
                    vAddrToSend.push_back(CAddress(uintGolderIP,htons(8333),NODE_NETWORK));
                if (addrLocalHost.IsRoutable())
                    vAddrToSend.push_back(addrLocalHost);
                if (vAddrToSend.size()>0)
                {
                    printf("These Address will send to %s:\n",pnode->addr.ToString().c_str());
                    for(unsigned int i=0;i<vAddrToSend.size();i++)
                        printf("%s\n",vAddrToSend[i].ToString().c_str());
                    pnode->PushMessage("addr", vAddrToSend);
                }




                // Get as many addresses as we can
                pnode->PushMessage("getaddr");

                ////// should the one on the receiving end do this too?
                // Subscribe our local subscription list
                const unsigned int nHops = 0;
                for (unsigned int nChannel = 0; nChannel < pnodeLocalHost->vfSubscribe.size(); nChannel++)
                    if (pnodeLocalHost->vfSubscribe[nChannel])
                        pnode->PushMessage("subscribe", nChannel, nHops);

                fSuccess = true;
                break;
            }
        }
    }
}








void ThreadMessageHandler(void* parg)
{
    IMPLEMENT_RANDOMIZE_STACK(ThreadMessageHandler(parg));

    loop
    {
        vfThreadRunning[2] = true;
        CheckForShutdown(2);
        try
        {
            ThreadMessageHandler2(parg);
        }
        CATCH_PRINT_EXCEPTION("ThreadMessageHandler()")
        vfThreadRunning[2] = false;
        Sleep(5000);
    }
}

void ThreadMessageHandler2(void* parg)
{
    printf("ThreadMessageHandler started\n");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    loop
    {
        // Poll the connected nodes for messages
        vector<CNode*> vNodesCopy;
        CRITICAL_BLOCK(cs_vNodes)
            vNodesCopy = vNodes;

		Sleep(100);
        foreach(CNode* pnode, vNodesCopy)
        {
            pnode->AddRef();

            // Receive messages
            TRY_CRITICAL_BLOCK(pnode->cs_vRecv)
                ProcessMessages(pnode);

            // Send messages
            TRY_CRITICAL_BLOCK(pnode->cs_vSend)
                SendMessages(pnode);

            pnode->Release();
        }

        // Wait and allow messages to bunch up
        vfThreadRunning[2] = false;
        Sleep(100);
        vfThreadRunning[2] = true;
        CheckForShutdown(2);
    }
}









//// todo: start one thread per processor, use getenv("NUMBER_OF_PROCESSORS")
void ThreadBitcoinMiner(void* parg)
{
    vfThreadRunning[3] = true;
    CheckForShutdown(3);
    try
    {
        bool fRet = BitcoinMiner();
        printf("BitcoinMiner returned %s\n\n\n", fRet ? "true" : "false");
    }
    CATCH_PRINT_EXCEPTION("BitcoinMiner()")
    vfThreadRunning[3] = false;
}











bool StartNode(string& strError)
{
    strError = "";

    // Sockets startup
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR)
    {
        strError = strprintf("Error: TCP/IP socket library failed to start (WSAStartup returned error %d)", ret);
        printf("%s\n", strError.c_str());
        return false;
    }
	
	// Clear and populate local IPv4 addresses set
    setLocalHostIPv4.clear();
    
    // Add loopback address (127.0.0.1)
    setLocalHostIPv4.insert(inet_addr("127.0.0.1"));
    
    // Add wildcard address (0.0.0.0)
    setLocalHostIPv4.insert(inet_addr("0.0.0.0"));
    
    // Add addrLocalHost's IP
    setLocalHostIPv4.insert(addrLocalHost.ip);

    // Get local host ip
    char pszHostName[255];
    if (gethostname(pszHostName, 255) == SOCKET_ERROR)
    {
        strError = strprintf("Error: Unable to get IP address of this computer (gethostname returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }
    struct hostent* pHostEnt = gethostbyname(pszHostName);
    if (!pHostEnt)
    {
        strError = strprintf("Error: Unable to get IP address of this computer (gethostbyname returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }
	for (int i = 0; pHostEnt->h_addr_list[i] != NULL; i++)
    {
        unsigned int localIp = *(unsigned int*)pHostEnt->h_addr_list[i];
        printf("Local host ipv4 %d: %s\n", i, CAddress(localIp).ToStringIP().c_str());
        // Add to local IP set
        setLocalHostIPv4.insert(localIp);
    }
	
    //addrLocalHost = CAddress(*(long*)(pHostEnt->h_addr_list[0]),
    //                         DEFAULT_PORT,
    //                         nLocalServices);
	addrLocalHost = CAddress("0.0.0.0:8333",nLocalServices);
    printf("addrLocalHost = %s\n", addrLocalHost.ToString().c_str());

    // Create socket for listening for incoming connections
    SOCKET hListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

    // Set to nonblocking, incoming connections will also inherit this
    u_long nOne = 1;
    if (ioctlsocket(hListenSocket, FIONBIO, &nOne) == SOCKET_ERROR)
    {
        strError = strprintf("Error: Couldn't set properties on socket for incoming connections (ioctlsocket returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound
    int nRetryLimit = 15;
    struct sockaddr_in sockaddr = addrLocalHost.GetSockAddr();
    if (bind(hListenSocket, (struct sockaddr*)&sockaddr, sizeof(sockaddr)) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf("Error: Unable to bind to port %s on this computer. The program is probably already running.", addrLocalHost.ToString().c_str());
        else
            strError = strprintf("Error: Unable to bind to port %s on this computer (bind returned error %d)", addrLocalHost.ToString().c_str(), nErr);
        printf("%s\n", strError.c_str());
        return false;
    }
    printf("bound to addrLocalHost = %s\n\n", addrLocalHost.ToString().c_str());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf("Error: Listening for incoming connections failed (listen returned error %d)", WSAGetLastError());
        printf("%s\n", strError.c_str());
        return false;
    }

    // Get our external IP address for incoming connections
    if (GetMyExternalIP(addrIncoming.ip))
    {
        CWalletDB().WriteSetting("addrIncoming", addrIncoming);
		printf("addrIncoming = %s\n", addrIncoming.ToString().c_str());
		setLocalHostIPv4.insert(addrIncoming.ip);
    }

    // Get addresses from IRC and advertise ours
    if (_beginthread(ThreadIRCSeed, 0, NULL) == -1)
        printf("Error: _beginthread(ThreadIRCSeed) failed\n");

    //
    // Start threads
    //
    if (_beginthread(ThreadSocketHandler, 0, new SOCKET(hListenSocket)) == -1)
    {
        strError = "Error: _beginthread(ThreadSocketHandler) failed";
        printf("%s\n", strError.c_str());
        return false;
    }

    if (_beginthread(ThreadOpenConnections, 0, NULL) == -1)
    {
        strError = "Error: _beginthread(ThreadOpenConnections) failed";
        printf("%s\n", strError.c_str());
        return false;
    }

    if (_beginthread(ThreadMessageHandler, 0, NULL) == -1)
    {
        strError = "Error: _beginthread(ThreadMessageHandler) failed";
        printf("%s\n", strError.c_str());
        return false;
    }

    return true;
}

bool StopNode()
{
    printf("StopNode()\n");
    fShutdown = true;
    nTransactionsUpdated++;
    int64 nStart = GetTime();
    while (vfThreadRunning[0] || vfThreadRunning[2] || vfThreadRunning[3])
    {
        if (GetTime() - nStart > 15)
            break;
        Sleep(20);
    }
    if (vfThreadRunning[0]) printf("ThreadSocketHandler still running\n");
    if (vfThreadRunning[1]) printf("ThreadOpenConnections still running\n");
    if (vfThreadRunning[2]) printf("ThreadMessageHandler still running\n");
    if (vfThreadRunning[3]) printf("ThreadBitcoinMiner still running\n");
    while (vfThreadRunning[2])
        Sleep(20);
    Sleep(50);

    // Sockets shutdown
    WSACleanup();
    return true;
}

void CheckForShutdown(int n)
{
    if (fShutdown)
    {
        if (n != -1)
            vfThreadRunning[n] = false;
        _endthread();
    }
}

//
// CAddress class member
//
bool CAddress::IsLocalHostIPv4() const
{
    if (!IsIPv4())
        return false;
    
    // Check if the IP of the current address is in the local IPv4 address set
    return (setLocalHostIPv4.find(ip) != setLocalHostIPv4.end());
}
bool CAddress::IsLocalListenAddr() const
{
	return (IsLocalHostIPv4() && port==addrLocalHost.port);
}
