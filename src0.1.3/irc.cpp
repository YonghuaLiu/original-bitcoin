// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"




#pragma pack(1)
struct ircaddr
{
    int ip;
    short port;
};

string EncodeAddress(const CAddress& addr)
{
    struct ircaddr tmp;
    tmp.ip    = addr.ip;
    tmp.port  = addr.port;

    vector<unsigned char> vch(UBEGIN(tmp), UEND(tmp));
    return string("u") + EncodeBase58Check(vch);
}

bool DecodeAddress(string str, CAddress& addr)
{
    vector<unsigned char> vch;
    if (!DecodeBase58Check(str.substr(1), vch))
        return false;

    struct ircaddr tmp;
    if (vch.size() != sizeof(tmp))
        return false;
    memcpy(&tmp, &vch[0], sizeof(tmp));

    addr  = CAddress(tmp.ip, tmp.port);
    return true;
}






static bool Send(SOCKET hSocket, const char* pszSend)
{
    if (strstr(pszSend, "PONG") != pszSend)
        printf("SENDING: %s\n", pszSend);
    const char* psz = pszSend;
    const char* pszEnd = psz + strlen(psz);
    while (psz < pszEnd)
    {
        int ret = send(hSocket, psz, pszEnd - psz, 0);
        if (ret < 0)
            return false;
        psz += ret;
    }
	// 新增：需要时追加发送消息尾巴 \r\n
    if (strlen(pszSend) >= 2 && memcmp(pszSend + strlen(pszSend) - 2, "\r\n", 2) != 0)
    {
        int ret = send(hSocket, "\r\n", 2, 0);
        if (ret < 0)
            return false;
    }
    return true;
}

bool RecvLine(SOCKET hSocket, string& strLine)
{
    strLine = "";
    loop
    {
        char c;
        int nBytes = recv(hSocket, &c, 1, 0);
        if (nBytes > 0)
        {
            if (c == '\n')
                continue;
            if (c == '\r')
                return true;
            strLine += c;
        }
        else if (nBytes <= 0)
        {
            if (!strLine.empty())
                return true;
            // socket closed
            printf("IRC socket closed\n");
            return false;
        }
        else
        {
            // socket error
            int nErr = WSAGetLastError();
            if (nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
            {
                printf("IRC recv failed: %d\n", nErr);
                return false;
            }
        }
    }
}

bool RecvLineIRC(SOCKET hSocket, string& strLine)
{
    //printf("IRC RecvLineIRC start... ! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    loop
    {
        bool fRet = RecvLine(hSocket, strLine);
        if (fRet)
        {
            //printf("IRC RecvLineIRC recived message line:[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            if (fShutdown)
                return false;
            vector<string> vWords;
            ParseString(strLine, ' ', vWords);
			//printf("IRC RecvLineIRC ParseString done! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            if (vWords[0] == "PING")
            {
				//printf("IRC RecvLineIRC recived PING message[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
                strLine[1] = 'O';
                strLine += "\r\n";
                Send(hSocket, strLine.c_str());
				//printf("IRC RecvLineIRC send PONG done[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
                continue;
            }
			if (vWords[0] == "ERROR")
			{
				printf("IRC RecvLineIRC recived ERROR message[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
				return false;
			}
        }
		//printf("IRC RecvLineIRC full done!strLine:[%s]. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        return fRet;
    }
}

bool RecvUntil(SOCKET hSocket, const char* psz1, const char* psz2=NULL, const char* psz3=NULL)
{
    //printf("IRC RecvUntil start... ![%s][%s][%s] Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",psz1,psz2,psz3,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    loop
    {
        string strLine;
        if (!RecvLineIRC(hSocket, strLine))
            return false;
        printf("IRC %s\n", strLine.c_str());
        if (psz1 && strLine.find(psz1) != -1)
            return true;
        if (psz2 && strLine.find(psz2) != -1)
            return true;
        if (psz3 && strLine.find(psz3) != -1)
            return true;
        //printf("IRC RecvUntil no care message line:[%s]. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strLine.c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    }
}

bool Wait(int nSeconds)
{
    if (fShutdown)
        return false;
    printf("Waiting %d seconds to reconnect to IRC\n", nSeconds);
    for (int i = 0; i < nSeconds; i++)
    {
        if (fShutdown)
            return false;
        Sleep(1000);
    }
    return true;
}



void ThreadIRCSeed(void* parg)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    int nErrorWait = 30;
    int nRetryWait = 10;
    printf("IRCSeed nRetryWait=%d. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nRetryWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

    while (!fShutdown)
    {
        //printf("IRCSeed nRetryWait=%d. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nRetryWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        //struct hostent* phostent = gethostbyname("chat.freenode.net");
        struct hostent* phostent = gethostbyname("irc.freenode.net");  //irc.freenode.net
        CAddress addrConnect(*(u_long*)phostent->h_addr_list[0], htons(6667));

        //printf("IRCSeed nRetryWait=%d. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nRetryWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        SOCKET hSocket;
        if (!ConnectSocket(addrConnect, hSocket))
        {
            printf("IRC connect failed\n");
            if (Wait(nErrorWait += 60))
                continue;
            else
                return;
        }
        //printf("IRC connect success.\n");
        //printf("IRC connect success. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

        //if (!RecvUntil(hSocket, "Found your hostname", "using your IP address instead", "Couldn't look up your hostname"))
		if (!RecvUntil(hSocket, "Found your hostname", "using your IP address ", "Couldn't look up your hostname"))
        {
            printf("IRC RecvUntil failed.Wait %d seconds. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nErrorWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            closesocket(hSocket);
            if (Wait(nErrorWait += 60))
                continue;
            else
                return;
        }
        //printf("IRC RecvUntil success. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

        string strMyName = EncodeAddress(addrLocalHost);

        if (!addrLocalHost.IsRoutable())
            strMyName = strprintf("x%u", GetRand(1000000000));

        printf("On IRC my name is %s. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strMyName.c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

        //Send(hSocket, strprintf("NICK %s\r", strMyName.c_str()).c_str());
		Send(hSocket, strprintf("NICK %s\r\n", strMyName.c_str()).c_str());
        printf("IRC Send done:%s . Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strprintf("NICK %s\r", strMyName.c_str()).c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        if(!Wait(3))
			return ;
		//Send(hSocket, strprintf("USER %s 8 * : %s\r", strMyName.c_str(), strMyName.c_str()).c_str());
		Send(hSocket, strprintf("USER %s 8 * : %s\r\n", strMyName.c_str(), strMyName.c_str()).c_str());
        printf("IRC Send done:%s . Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",strprintf("USER %s 8 * : %s\r", strMyName.c_str(), strMyName.c_str()).c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

        if (!RecvUntil(hSocket,":*.freenode.net NOTICE","PING :"," 004 "))
        {
            printf("IRC RecvUntil failed.Wait %d seconds. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nErrorWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            closesocket(hSocket);
            if (Wait(nErrorWait += 60))
                continue;
            else
                return;
        }
		
		//printf("IRC PING/PONG OK! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n", GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);

        Send(hSocket, "JOIN #bitcoin");
        //printf("IRC Send done:%s . Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n", "JOIN #bitcoin\r",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        if(!Wait(3))
			return ;
		Send(hSocket, "WHO #bitcoin");
        //printf("IRC Send done:%s . Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n", "WHO #bitcoin\r",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
		//if(!Wait(3))
		//	return ;

        string strLine;
        while (!fShutdown && RecvLineIRC(hSocket, strLine))
        {
            //printf("IRC Recived a message:%s .\n Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n", strLine.c_str() ,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
            if (strLine.empty() || strLine.size() > 900 || strLine[0] != ':')
                continue;
            printf("IRC %s\n", strLine.c_str());

            vector<string> vWords;
            ParseString(strLine, ' ', vWords);
            if (vWords.size() < 2)
                continue;

            char pszName[10000];
            pszName[0] = '\0';

            if (vWords[1] == "352" && vWords.size() >= 8)
            {
                // index 7 is limited to 16 characters
                // could get full length name at index 10, but would be different from join messages
                strcpy(pszName, vWords[7].c_str());
                printf("GOT WHO: [%s]  ", pszName);
            }

            if (vWords[1] == "JOIN" && vWords[0].size() > 1)
            {
                // :username!username@50000007.F000000B.90000002.IP JOIN :#channelname
                strcpy(pszName, vWords[0].c_str() + 1);
                if (strchr(pszName, '!'))
                    *strchr(pszName, '!') = '\0';
                printf("GOT JOIN: [%s]  ", pszName);
            }

            if (pszName[0] == 'u')
            {
                CAddress addr;
                if (DecodeAddress(pszName, addr))
                {
                    CAddrDB addrdb;
                    if (AddAddress(addrdb, addr))
                        printf("new  ");
                    addr.print();
                }
                else
                {
                    printf("decode failed\n");
                }
            }

        }
        closesocket(hSocket);

        if (!Wait(nRetryWait += 10))
            return;
        printf("IRCSeed nRetryWait=%d. Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",nRetryWait,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    }
}










#ifdef TEST
int main(int argc, char *argv[])
{
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2,2), &wsadata) != NO_ERROR)
    {
        printf("Error at WSAStartup()\n");
        return false;
    }

    ThreadIRCSeed(NULL);

    WSACleanup();
    return 0;
}
#endif
