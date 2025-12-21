// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"

//用于开发阶段支持内存监测功能
#include <wx/process.h>
#ifdef _WIN32
#include <psapi.h>  // Windows 平台依赖
#else
#include <sys/resource.h>  // Linux/macOS 依赖
#include <unistd.h>
#endif


bool fDebug = false;




// Init openssl library multithreading support
static HANDLE* lock_cs;

void win32_locking_callback(int mode, int type, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
        WaitForSingleObject(lock_cs[type], INFINITE);
    else
        ReleaseMutex(lock_cs[type]);
}

// Init
class CInit
{
public:
    CInit()
    {
        // Init openssl library multithreading support
        lock_cs = (HANDLE*)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(HANDLE));
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            lock_cs[i] = CreateMutex(NULL,FALSE,NULL);
        CRYPTO_set_locking_callback(win32_locking_callback);

        // Seed random number generator with screen scrape and other hardware sources
        RAND_screen();

        // Seed random number generator with perfmon data
        RandAddSeed(true);
    }
    ~CInit()
    {
        // Shutdown openssl library multithreading support
        CRYPTO_set_locking_callback(NULL);
        for (int i =0 ; i < CRYPTO_num_locks(); i++)
            CloseHandle(lock_cs[i]);
        OPENSSL_free(lock_cs);
    }
}
instance_of_cinit;




void RandAddSeed(bool fPerfmon)
{
    // Seed with CPU performance counter
    LARGE_INTEGER PerformanceCount;
    QueryPerformanceCounter(&PerformanceCount);
    RAND_add(&PerformanceCount, sizeof(PerformanceCount), 1.5);
    memset(&PerformanceCount, 0, sizeof(PerformanceCount));

    static int64 nLastPerfmon;
    if (fPerfmon || GetTime() > nLastPerfmon + 5 * 60)
    {
        nLastPerfmon = GetTime();

        // Seed with the entire set of perfmon data
        unsigned char pdata[250000];
        memset(pdata, 0, sizeof(pdata));
        unsigned long nSize = sizeof(pdata);
        long ret = RegQueryValueEx(HKEY_PERFORMANCE_DATA, L"Global", NULL, NULL, pdata, &nSize);
        RegCloseKey(HKEY_PERFORMANCE_DATA);
        if (ret == ERROR_SUCCESS)
        {
            uint256 hash;
            SHA256(pdata, nSize, (unsigned char*)&hash);
            RAND_add(&hash, sizeof(hash), min(nSize/500.0, (double)sizeof(hash)));
            hash = 0;
            memset(pdata, 0, nSize);

            time_t nTime;
            time(&nTime);
            struct tm* ptmTime = gmtime(&nTime);
            char pszTime[200];
            strftime(pszTime, sizeof(pszTime), "%x %H:%M:%S", ptmTime);
            printf("%s  RandAddSeed() got %d bytes of performance data\n", pszTime, nSize);
        }
    }
}










// Safer snprintf
//  - prints up to limit-1 characters
//  - output string is always null terminated even if limit reached
//  - return value is the number of characters actually printed
int my_snprintf(char* buffer, size_t limit, const char* format, ...)
{
    if (limit == 0)
        return 0;
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int ret = _vsnprintf(buffer, limit, format, arg_ptr);
    va_end(arg_ptr);
    if (ret < 0 || ret >= limit)
    {
        ret = limit - 1;
        buffer[limit-1] = 0;
    }
    return ret;
}


string strprintf(const char* format, ...)
{
    char buffer[50000];
    char* p = buffer;
    int limit = sizeof(buffer);
    int ret;
    loop
    {
        va_list arg_ptr;
        va_start(arg_ptr, format);
        ret = _vsnprintf(p, limit, format, arg_ptr);
        va_end(arg_ptr);
        if (ret >= 0 && ret < limit)
            break;
        if (p != buffer)
            delete p;
        limit *= 2;
        p = new char[limit];
        if (p == NULL)
            throw std::bad_alloc();
    }
#ifdef _MSC_VER
    // msvc optimisation
    if (p == buffer)
        return string(p, p+ret);
#endif
    string str(p, p+ret);
    if (p != buffer)
        delete p;
    return str;
}


bool error(const char* format, ...)
{
    char buffer[50000];
    int limit = sizeof(buffer);
    va_list arg_ptr;
    va_start(arg_ptr, format);
    int ret = _vsnprintf(buffer, limit, format, arg_ptr);
    va_end(arg_ptr);
    if (ret < 0 || ret >= limit)
    {
        ret = limit - 1;
        buffer[limit-1] = 0;
    }
    printf("ERROR: %s\n", buffer);
    return false;
}


void PrintException(std::exception* pex, const char* pszThread)
{
    char pszModule[260];
    pszModule[0] = '\0';
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
    _strlwr(pszModule);
    char pszMessage[1000];
    if (pex)
        snprintf(pszMessage, sizeof(pszMessage),
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        snprintf(pszMessage, sizeof(pszMessage),
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
    printf("\n\n************************\n%s", pszMessage);
    if (wxTheApp)
        wxMessageBox(pszMessage, "Error", wxOK | wxICON_ERROR);
    throw;
    //DebugBreak();
}


void ParseString(const string& str, char c, vector<string>& v)
{
    size_t i1 = 0;
    size_t i2;
    //printf("ParseString start[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",str.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
    do
    {
        i2 = str.find(c, i1);
        //printf("ParseString find a word[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",str.substr(i1, i2-i1).c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        v.push_back(str.substr(i1, i2-i1));
        i1 = i2+1;
        //printf("ParseString i2=[%zu]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",i2,GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
        //if(i2 == str.npos)
        //    printf("ParseString i2=npos,loop will break! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);//此行代码会被执行吗？
    }
    while (i2 < str.size()-1);
    //printf("ParseString done[%s]! Current Memory total():%.2lf MB\t\tCode at:%s:%d %s\n",str.c_str(),GetCurrentProcessMemoryMB(),GetFileNameWithoutPath(__FILE__),__LINE__,__FUNCTION__);
}


string FormatMoney(int64 n, bool fPlus)
{
    n /= CENT;
    string str = strprintf("%I64d.%02I64d", (n > 0 ? n : -n)/100, (n > 0 ? n : -n)%100);
    for (int i = 6; i < str.size(); i += 4)
        if (isdigit(str[str.size() - i - 1]))
            str.insert(str.size() - i, 1, ',');
    if (n < 0)
        str.insert((unsigned int)0, 1, '-');
    else if (fPlus && n > 0)
        str.insert((unsigned int)0, 1, '+');
    return str;
}

bool ParseMoney(const char* pszIn, int64& nRet)
{
    string strWhole;
    int64 nCents = 0;
    const char* p = pszIn;
    while (isspace(*p))
        p++;
    for (; *p; p++)
    {
        if (*p == ',' && p > pszIn && isdigit(p[-1]) && isdigit(p[1]) && isdigit(p[2]) && isdigit(p[3]) && !isdigit(p[4]))
            continue;
        if (*p == '.')
        {
            p++;
            if (!isdigit(p[0]) || !isdigit(p[1]))
                return false;
            nCents = atoi64(p);
            if (nCents < 0 || nCents > 99)
                return false;
            p += 2;
            break;
        }
        if (isspace(*p))
            break;
        if (!isdigit(*p))
            return false;
        strWhole.insert(strWhole.end(), *p);
    }
    for (; *p; p++)
        if (!isspace(*p))
            return false;
    if (strWhole.size() > 17)
        return false;
    int64 nWhole = atoi64(strWhole);
    int64 nValue = nWhole * 100 + nCents;
    if (nValue / 100 != nWhole)
        return false;
    nValue *= CENT;
    nRet = nValue;
    return true;
}










bool FileExists(const char* psz)
{
#ifdef WIN32
    return GetFileAttributesA(psz) != -1;
#else
    return access(psz, 0) != -1;
#endif
}

int GetFilesize(FILE* file)
{
    int nSavePos = ftell(file);
    int nFilesize = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        nFilesize = ftell(file);
    fseek(file, nSavePos, SEEK_SET);
    return nFilesize;
}








uint64 GetRand(uint64 nMax)
{
    if (nMax == 0)
        return 0;

    // The range of the random source must be a multiple of the modulus
    // to give every possible output value an equal possibility
    uint64 nRange = (_UI64_MAX / nMax) * nMax;
    uint64 nRand = 0;
    do
        RAND_bytes((unsigned char*)&nRand, sizeof(nRand));
    while (nRand >= nRange);
    return (nRand % nMax);
}










//
// "Never go to sea with two chronometers; take one or three."
// Our three chronometers are:
//  - System clock
//  - Median of other server's clocks
//  - NTP servers
//
// note: NTP isn't implemented yet, so until then we just use the median
//  of other nodes clocks to correct ours.
//

int64 GetTime()
{
    return time(NULL);
}

static int64 nTimeOffset = 0;

int64 GetAdjustedTime()
{
    return GetTime() + nTimeOffset;
}

void AddTimeData(unsigned int ip, int64 nTime)
{
    int64 nOffsetSample = nTime - GetTime();

    // Ignore duplicates
    static set<unsigned int> setKnown;
    if (!setKnown.insert(ip).second)
        return;

    // Filter out obviously invalid time data
    // If the offset is more than 1 year in either direction, ignore it
    const int64 MAX_TIME_OFFSET = 365 * 24 * 60 * 60; // 1 year in seconds
    if (nOffsetSample > MAX_TIME_OFFSET || nOffsetSample < -MAX_TIME_OFFSET) {
        printf("Ignoring invalid time data from ip %08x, offset %+I64d (too large)\n", ip, nOffsetSample);
        return;
    }

    // Add data
    static vector<int64> vTimeOffsets;
    if (vTimeOffsets.empty())
        vTimeOffsets.push_back(0);
    vTimeOffsets.push_back(nOffsetSample);
    printf("Added time data, samples %d, ip %08x, offset %+I64d (%+I64d minutes)\n", vTimeOffsets.size(), ip, vTimeOffsets.back(), vTimeOffsets.back()/60);
    if (vTimeOffsets.size() >= 5 && vTimeOffsets.size() % 2 == 1)
    {
        sort(vTimeOffsets.begin(), vTimeOffsets.end());
        int64 nMedian = vTimeOffsets[vTimeOffsets.size()/2];
        nTimeOffset = nMedian;
        if ((nMedian > 0 ? nMedian : -nMedian) > 5 * 60)
        {
            // Only let other nodes change our clock so far before we
            // go to the NTP servers
            /// todo: Get time from NTP servers, then set a flag
            ///    to make sure it doesn't get changed again
        }
        foreach(int64 n, vTimeOffsets)
            printf("%+I64d  ", n);
        printf("|  nTimeOffset = %+I64d  (%+I64d minutes)\n", nTimeOffset, nTimeOffset/60);
    }
}



const char* GetFileNameWithoutPath(const char* fullPath) {
    if (!fullPath) return "";

    // thread_local：每个线程拥有独立的 fileName 副本，无竞争
    thread_local std::string fileName;
    std::string path(fullPath);
    size_t pos = path.find_last_of("/\\");

    if (pos != std::string::npos) {
        fileName = path.substr(pos + 1);
        return fileName.c_str();
    } else {
        return fullPath;
    }
}

// 通用函数：获取当前程序物理内存占用（单位：MB）,仅用于开发阶段支持内存监测功能
double GetCurrentProcessMemoryMB() {
    double memoryMB = 0.0;

#ifdef _WIN32
    // Windows 平台：通过 wxProcess 获取当前进程句柄，配合 PSAPI
    HANDLE hProcess = ::GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS pmc;
    if (::GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
        // WorkingSetSize = 物理内存占用（字节），转换为 MB
        memoryMB = static_cast<double>(pmc.WorkingSetSize) / 1024 / 1024;
    }
#else
    // Linux/macOS 平台（逻辑完全一致，合并为一个分支）
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // 两者 ru_maxrss 单位都是 KB，统一转换为 MB
        memoryMB = static_cast<double>(usage.ru_maxrss) / 1024;
    }
#endif

    return memoryMB;
}
