// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"






//
// CDB
//

static CCriticalSection cs_db;
static bool fDbEnvInit = false;
//DbEnv dbenv(0);
DbEnv dbenv(u_int32_t(0));
static map<string, int> mapFileUseCount;

class CDBInit
{
public:
    CDBInit()
    {
    }
    ~CDBInit()
    {
        if (fDbEnvInit)
        {
            dbenv.close(0);
            fDbEnvInit = false;
        }
    }
}
instance_of_cdbinit;


CDB::CDB(const char* pszFile, const char* pszMode, bool fTxn) : pdb(NULL)
{
    int ret;
    if (pszFile == NULL)
        return;

    bool fCreate = strchr(pszMode, 'c');
    bool fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;
    else if (fReadOnly)
        nFlags |= DB_RDONLY;
    if (!fReadOnly || fTxn)
        nFlags |= DB_AUTO_COMMIT;

    CRITICAL_BLOCK(cs_db)
    {
        if (!fDbEnvInit)
        {
            string strAppDir = GetAppDir();
            string strLogDir = strAppDir + "\\database";
            //_mkdir(strLogDir.c_str());
            mkdir(strLogDir.c_str());
            printf("dbenv.open strAppDir=%s\n", strAppDir.c_str());

            dbenv.set_lg_dir(strLogDir.c_str());
            dbenv.set_lg_max(10000000);
            dbenv.set_lk_max_locks(10000);
            dbenv.set_lk_max_objects(10000);
            dbenv.set_errfile(fopen("db.log", "a")); /// debug
            ///dbenv.log_set_config(DB_LOG_AUTO_REMOVE, 1); /// causes corruption
            // �������ã�Ϊȷ����ͬ�汾��������������ʹ��ͬһ�������ļ������а汾��������ִ�У�
            dbenv.set_cachesize(0, 1024*1024, 1); // �����Сͳһ������߰汾Ĭ�ϸ��󻺴浼�³�ͻ��
            //dbenv.set_flags(DB_STRICT, 0);        // �����ϸ�ģʽ��6.x ������
            dbenv.set_flags(DB_AUTO_COMMIT, 1);   // ͳһ�����Զ��ύ������ 4.x��
            dbenv.set_flags(DB_TXN_NOSYNC, 1);    // �����ύ��ˢ�̣�����ֵ��
            ret = dbenv.open(strAppDir.c_str(),
                             DB_CREATE     |
                             DB_INIT_LOCK  |
                             DB_INIT_LOG   |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN   |
                             DB_THREAD     |
                             DB_PRIVATE    |
                             DB_RECOVER,
                             0);
            if (ret > 0)
                throw runtime_error(strprintf("CDB() : error %d opening database environment\n", ret));

            printf("CDB() : Berkeley DB config completed. ret:%d \n", ret);

            fDbEnvInit = true;
        }

        strFile = pszFile;
        ++mapFileUseCount[strFile];
    }

    pdb = new Db(&dbenv, 0);
	// �ؼ����������ݿ��ļ���ʽΪ 4.8 �汾��DB_OLD_FORMAT �ȼ��� 4.8 ��ʽ��
    //u_int32_t fmt = DB_OLD_FORMAT; // ��ֱ��ָ���汾�ţ��� 0x04080000��4.8 �汾�ĸ�ʽ��ʶ��
    //ret = pdb->fcntl(DB_FCNTL_SET_FMT, &fmt);
	//ret = db_fcntl(pdb->get_DB(), DB_FCNTL_SET_FMT, &fmt);
    //if (ret != 0)
    //    throw runtime_error(strprintf("CDB() : error %d set DB format failed.\n", ret));

    //u_int32_t page_size = 4096;  // ����ҳ��С��Ϊ 4KB������ BDB �汾��֧�֣�
    //ret = pdb->fcntl(DB_FCNTL_SET_PGSIZE, &page_size); // ����ֵ��������
	////ret = db_fcntl(pdb->get_DB(), DB_FCNTL_SET_PGSIZE_VAL, &page_size);
    //if (ret != 0)
    //{
	//	delete pdb;
	//	pdb = NULL;
	//	CRITICAL_BLOCK(cs_db)
	//		--mapFileUseCount[strFile];
	//	strFile = "";
	//	throw runtime_error(strprintf("CDB() : error %d set page_size failed.\n",  ret));
    //}

    ret = pdb->open(NULL,      // Txn pointer
                    pszFile,   // Filename
                    "main",    // Logical db name
                    DB_BTREE,  // Database type
                    nFlags,    // Flags
                    0);

    if (ret > 0)
    {
        delete pdb;
        pdb = NULL;
        CRITICAL_BLOCK(cs_db)
            --mapFileUseCount[strFile];
        strFile = "";
        throw runtime_error(strprintf("CDB() : can't open database file %s, error %d\n", pszFile, ret));
    }


    if (fCreate && !Exists(string("version")))
        WriteVersion(VERSION);

    RandAddSeed();
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (!vTxn.empty())
        vTxn.front()->abort();
    vTxn.clear();
    pdb->close(0);
    delete pdb;
    pdb = NULL;
    dbenv.txn_checkpoint(0, 0, 0);

    CRITICAL_BLOCK(cs_db)
        --mapFileUseCount[strFile];

    RandAddSeed();
}

void DBFlush(bool fShutdown)
{
    // Flush log data to the actual data file
    //  on all files that are not in use
    printf("DBFlush(%s)\n", fShutdown ? "true" : "false");
    CRITICAL_BLOCK(cs_db)
    {
        dbenv.txn_checkpoint(0, 0, 0);
        map<string, int>::iterator mi = mapFileUseCount.begin();
        while (mi != mapFileUseCount.end())
        {
            string strFile = (*mi).first;
            int nRefCount = (*mi).second;
            if (nRefCount == 0)
            {
                dbenv.lsn_reset(strFile.c_str(), 0);
                mapFileUseCount.erase(mi++);
            }
            else
                mi++;
        }
        if (fShutdown)
        {
            char** listp;
            if (mapFileUseCount.empty())
                dbenv.log_archive(&listp, DB_ARCH_REMOVE);
            dbenv.close(0);
            fDbEnvInit = false;
        }
    }
}






//
// CTxDB
//

bool CTxDB::ReadTxIndex(uint256 hash, CTxIndex& txindex)
{
    assert(!fClient);
    txindex.SetNull();
    return Read(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::UpdateTxIndex(uint256 hash, const CTxIndex& txindex)
{
    assert(!fClient);
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::AddTxIndex(const CTransaction& tx, const CDiskTxPos& pos, int nHeight)
{
    assert(!fClient);

    // Add to tx index
    uint256 hash = tx.GetHash();
    CTxIndex txindex(pos, tx.vout.size());
    return Write(make_pair(string("tx"), hash), txindex);
}

bool CTxDB::EraseTxIndex(const CTransaction& tx)
{
    assert(!fClient);
    uint256 hash = tx.GetHash();

    return Erase(make_pair(string("tx"), hash));
}

bool CTxDB::ContainsTx(uint256 hash)
{
    assert(!fClient);
    return Exists(make_pair(string("tx"), hash));
}

bool CTxDB::ReadOwnerTxes(uint160 hash160, int nMinHeight, vector<CTransaction>& vtx)
{
    assert(!fClient);
    vtx.clear();

    // Get cursor
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        return false;

    unsigned int fFlags = DB_SET_RANGE;
    loop
    {
        // Read next record
        CDataStream ssKey;
        if (fFlags == DB_SET_RANGE)
            ssKey << string("owner") << hash160 << CDiskTxPos(0, 0, 0);
        CDataStream ssValue;
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
            return false;

        // Unserialize
        string strType;
        uint160 hashItem;
        CDiskTxPos pos;
        ssKey >> strType >> hashItem >> pos;
        int nItemHeight;
        ssValue >> nItemHeight;

        // Read transaction
        if (strType != "owner" || hashItem != hash160)
            break;
        if (nItemHeight >= nMinHeight)
        {
            vtx.resize(vtx.size()+1);
            if (!vtx.back().ReadFromDisk(pos))
                return false;
        }
    }
    return true;
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx, CTxIndex& txindex)
{
    assert(!fClient);
    tx.SetNull();
    if (!ReadTxIndex(hash, txindex))
        return false;
    return (tx.ReadFromDisk(txindex.pos));
}

bool CTxDB::ReadDiskTx(uint256 hash, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx, CTxIndex& txindex)
{
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::ReadDiskTx(COutPoint outpoint, CTransaction& tx)
{
    CTxIndex txindex;
    return ReadDiskTx(outpoint.hash, tx, txindex);
}

bool CTxDB::WriteBlockIndex(const CDiskBlockIndex& blockindex)
{
    return Write(make_pair(string("blockindex"), blockindex.GetBlockHash()), blockindex);
}

bool CTxDB::EraseBlockIndex(uint256 hash)
{
    return Erase(make_pair(string("blockindex"), hash));
}

bool CTxDB::ReadHashBestChain(uint256& hashBestChain)
{
    return Read(string("hashBestChain"), hashBestChain);
}

bool CTxDB::WriteHashBestChain(uint256 hashBestChain)
{
    return Write(string("hashBestChain"), hashBestChain);
}

CBlockIndex* InsertBlockIndex(uint256 hash)
{
    if (hash == 0)
        return NULL;

    // Return existing
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool CTxDB::LoadBlockIndex()
{
    // Get cursor
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        return false;

    unsigned int fFlags = DB_SET_RANGE;
    loop
    {
        // Read next record
        CDataStream ssKey;
        if (fFlags == DB_SET_RANGE)
            ssKey << make_pair(string("blockindex"), uint256(0));
        CDataStream ssValue;
        int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
        fFlags = DB_NEXT;
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
            return false;

        // Unserialize
        string strType;
        ssKey >> strType;
        if (strType == "blockindex")
        {
            CDiskBlockIndex diskindex;
            ssValue >> diskindex;

            // Construct block index object
            CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
            pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
            pindexNew->pnext          = InsertBlockIndex(diskindex.hashNext);
            pindexNew->nFile          = diskindex.nFile;
            pindexNew->nBlockPos      = diskindex.nBlockPos;
            pindexNew->nHeight        = diskindex.nHeight;
            pindexNew->nVersion       = diskindex.nVersion;
            pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
            pindexNew->nTime          = diskindex.nTime;
            pindexNew->nBits          = diskindex.nBits;
            pindexNew->nNonce         = diskindex.nNonce;

            //printf("Block info:[nHeight:%d,hash:%s,nTime:%u,nVersion:%d,hashMerkleRoot:%s,nBits:%u]\n"
            //       ,pindexNew->nHeight
            //       ,diskindex.GetBlockHash().ToString().substr(0,14).c_str()
            //       ,pindexNew->nTime
            //       ,pindexNew->nVersion
            //       ,pindexNew->hashMerkleRoot.ToString().substr(0,14).c_str()
            //       ,pindexNew->nBits);


            // Watch for genesis block and best block
            if (pindexGenesisBlock == NULL && diskindex.GetBlockHash() == hashGenesisBlock)
                pindexGenesisBlock = pindexNew;
        }
        else
        {
            break;
        }
    }

    if (!ReadHashBestChain(hashBestChain))
    {
        if (pindexGenesisBlock == NULL)
            return true;
        return error("CTxDB::LoadBlockIndex() : hashBestChain not found\n");
    }

    if (!mapBlockIndex.count(hashBestChain))
        return error("CTxDB::LoadBlockIndex() : blockindex for hashBestChain not found\n");
    pindexBest = mapBlockIndex[hashBestChain];
    nBestHeight = pindexBest->nHeight;
    printf("LoadBlockIndex(): hashBestChain=%s  height=%d\n", hashBestChain.ToString().substr(0,14).c_str(), nBestHeight);

    return true;
}





//
// CAddrDB
//

bool CAddrDB::WriteAddress(const CAddress& addr)
{
    return Write(make_pair(string("addr"), addr.GetKey()), addr);
}

bool CAddrDB::LoadAddresses()
{
    CRITICAL_BLOCK(cs_mapAddresses)
    {
        // Load user provided addresses
        CAutoFile filein = fopen("addr.txt", "rt");
        if (filein)
        {
            try
            {
                char psz[1000];
                while (fgets(psz, sizeof(psz), filein))
                {
                    printf("Read line text:%s\n",psz);
                    CAddress addr(psz, NODE_NETWORK);
                    if (addr.ip != 0)
                    {
                        printf("AddAddress:%s.\n",addr.ToString().c_str() );
                        AddAddress(*this, addr);
                    }
                    else
                        printf("Paser address failed:%.\n",psz );

                }
            }
            catch (...) { }
        }

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor)
            return false;

        loop
        {
            // Read next record
            CDataStream ssKey;
            CDataStream ssValue;
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
                return false;

            // Unserialize
            string strType;
            ssKey >> strType;
            //printf("strType:%s\n",strType.c_str());
            if (strType == "addr")
            {
                CAddress addr;
                ssValue >> addr;
                //printf("Read addr:%s\n",addr.ToString().c_str());
                mapAddresses.insert(make_pair(addr.GetKey(), addr));
            }
        }

        //// debug print
        //printf("mapAddresses:\n");
        //foreach(const PAIRTYPE(vector<unsigned char>, CAddress)& item, mapAddresses)
        //    item.second.print();
        //printf("-----\n");
		
		printf("Loaded %d addresses\n", mapAddresses.size());
		for (const auto& [key, value] : mapAddresses) 
		{
			std::string key_str;
			for (const auto& i : key) key_str += strprintf("%u.",i);
			printf("[%s = %s]\n",key_str.c_str(),value.ToString().c_str());
		}

        // Fix for possible GCC bug that manifests in mapAddresses.count in irc.cpp,
        // just need to call count here and it doesn't happen there, do not delete this!
        mapAddresses.count(vector<unsigned char>(18));
    }

    return true;
}

bool LoadAddresses()
{
    return CAddrDB("cr+").LoadAddresses();
}




//
// CReviewDB
//

bool CReviewDB::ReadReviews(uint256 hash, vector<CReview>& vReviews)
{
    vReviews.size(); // msvc workaround, just need to do anything with vReviews
    return Read(make_pair(string("reviews"), hash), vReviews);
}

bool CReviewDB::WriteReviews(uint256 hash, const vector<CReview>& vReviews)
{
    return Write(make_pair(string("reviews"), hash), vReviews);
}







//
// CWalletDB
//

bool CWalletDB::LoadWallet(vector<unsigned char>& vchDefaultKeyRet)
{
    vchDefaultKeyRet.clear();

    //// todo: shouldn't we catch exceptions and try to recover and continue?
    CRITICAL_BLOCK(cs_mapKeys)
    CRITICAL_BLOCK(cs_mapWallet)
    {
        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor)
            return false;

        loop
        {
            // Read next record
            CDataStream ssKey;
            CDataStream ssValue;
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
                return false;

            // Unserialize
            // Taking advantage of the fact that pair serialization
            // is just the two items serialized one after the other
            string strType;
            ssKey >> strType;
            if (strType == "name")
            {
                string strAddress;
                ssKey >> strAddress;
                ssValue >> mapAddressBook[strAddress];
            }
            else if (strType == "tx")
            {
                uint256 hash;
                ssKey >> hash;
                CWalletTx& wtx = mapWallet[hash];
                ssValue >> wtx;

                if (wtx.GetHash() != hash)
                    printf("Error in wallet.dat, hash mismatch\n");

                //// debug print
                //printf("LoadWallet  %s\n", wtx.GetHash().ToString().c_str());
                //printf(" %12I64d  %s  %s  %s\n",
                //    wtx.vout[0].nValue,
                //    DateTimeStr(wtx.nTime).c_str(),
                //    wtx.hashBlock.ToString().substr(0,14).c_str(),
                //    wtx.mapValue["message"].c_str());
            }
            else if (strType == "key")
            {
                vector<unsigned char> vchPubKey;
                ssKey >> vchPubKey;
                CPrivKey vchPrivKey;
                ssValue >> vchPrivKey;

                mapKeys[vchPubKey] = vchPrivKey;
                mapPubKeys[Hash160(vchPubKey)] = vchPubKey;
            }
            else if (strType == "defaultkey")
            {
                ssValue >> vchDefaultKeyRet;
            }
            else if (strType == "setting")  /// or settings or option or options or config?
            {
                string strKey;
                ssKey >> strKey;
                if (strKey == "fGenerateBitcoins")  ssValue >> fGenerateBitcoins;
                if (strKey == "nTransactionFee")    ssValue >> nTransactionFee;
                if (strKey == "addrIncoming")       ssValue >> addrIncoming;
            }
        }
    }

    printf("fGenerateBitcoins = %d\n", fGenerateBitcoins);
    printf("nTransactionFee = %I64d\n", nTransactionFee);
    printf("addrIncoming = %s\n", addrIncoming.ToString().c_str());

    return true;
}

bool LoadWallet()
{
    vector<unsigned char> vchDefaultKey;
    if (!CWalletDB("cr").LoadWallet(vchDefaultKey))
        return false;

    if (mapKeys.count(vchDefaultKey))
    {
        // Set keyUser
        keyUser.SetPubKey(vchDefaultKey);
        keyUser.SetPrivKey(mapKeys[vchDefaultKey]);
    }
    else
    {
        // Create new keyUser and set as default key
        keyUser.MakeNewKey();
        if (!AddKey(keyUser))
            return false;
        if (!SetAddressBookName(PubKeyToAddress(keyUser.GetPubKey()), "Your Address"))
            return false;
        CWalletDB().WriteDefaultKey(keyUser.GetPubKey());
    }

    return true;
}
