// Copyright 2023 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "SorobanTxTestUtils.h"
#include "lib/catch.hpp"
#include "rust/RustBridge.h"
#include "test/TestAccount.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "transactions/InvokeHostFunctionOpFrame.h"
#include "transactions/TransactionUtils.h"
#include "xdrpp/printer.h"

namespace stellar
{

namespace txtest
{
namespace
{
SCVal
signPayloadForClassicAccount(std::vector<TestAccount*> const& signers,
                             uint256 payload)
{
    SCVal signatureStruct(SCV_VEC);
    auto& signatures = signatureStruct.vec().activate();
    auto sortedSigners = signers;
    std::sort(sortedSigners.begin(), sortedSigners.end(),
              [](TestAccount* a, TestAccount* b) {
                  return a->getPublicKey() < b->getPublicKey();
              });
    for (auto& account : sortedSigners)
    {
        SCVal signatureVal(SCV_MAP);
        signatureVal.map().activate().emplace_back(
            makeSymbolSCVal("public_key"),
            makeBytesSCVal(account->getPublicKey().ed25519()));
        auto signature = account->getSecretKey().sign(payload);
        signatureVal.map().activate().emplace_back(makeSymbolSCVal("signature"),
                                                   makeBytesSCVal(signature));
        signatures.push_back(signatureVal);
    }
    return signatureStruct;
}
} // namespace

SCAddress
makeContractAddress(Hash const& hash)
{
    SCAddress addr(SC_ADDRESS_TYPE_CONTRACT);
    addr.contractId() = hash;
    return addr;
}

SCAddress
makeAccountAddress(AccountID const& accountID)
{
    SCAddress addr(SC_ADDRESS_TYPE_ACCOUNT);
    addr.accountId() = accountID;
    return addr;
}

SCVal
makeI32(int32_t i32)
{
    SCVal val(SCV_I32);
    val.i32() = i32;
    return val;
}

SCVal
makeI128(uint64_t u64)
{
    Int128Parts p;
    p.hi = 0;
    p.lo = u64;

    SCVal val(SCV_I128);
    val.i128() = p;
    return val;
}

SCSymbol
makeSymbol(std::string const& str)
{
    SCSymbol val;
    val.assign(str.begin(), str.end());
    return val;
}

SCVal
makeU64(uint64_t u64)
{
    SCVal val(SCV_U64);
    val.u64() = u64;
    return val;
}

SCVal
makeU32(uint32_t u32)
{
    SCVal val(SCV_U32);
    val.u32() = u32;
    return val;
}

SCVal
makeVecSCVal(std::vector<SCVal> elems)
{
    SCVal val(SCV_VEC);
    val.vec().activate().assign(elems.begin(), elems.end());
    return val;
}

SCVal
makeBool(bool b)
{
    SCVal val(SCV_BOOL);
    val.b() = b;
    return val;
}

int64_t
getContractBalance(Application& app, SCAddress const& contractID,
                   SCVal const& accountVal)
{
    LedgerKey balanceKey(CONTRACT_DATA);
    balanceKey.contractData().contract = contractID;

    balanceKey.contractData().key =
        makeVecSCVal({makeSymbolSCVal("Balance"), accountVal});
    balanceKey.contractData().durability = ContractDataDurability::PERSISTENT;

    LedgerTxn ltx(app.getLedgerTxnRoot());
    auto ltxe = ltx.load(balanceKey);
    if (!ltxe)
    {
        return 0;
    }
    auto const& balance =
        ltxe.current().data.contractData().val.map()->at(0).val.i128();

    // Contract balances can be greater than INT64_MAX, but for these tests
    // we'll assume balances < INT64_MAX.
    REQUIRE(balance.hi == 0);
    return balance.lo;
}

ContractIDPreimage
makeContractIDPreimage(TestAccount& source, uint256 salt)
{
    ContractIDPreimage idPreimage(CONTRACT_ID_PREIMAGE_FROM_ADDRESS);
    idPreimage.fromAddress().address.type(SC_ADDRESS_TYPE_ACCOUNT);
    idPreimage.fromAddress().address.accountId().ed25519() =
        source.getPublicKey().ed25519();
    idPreimage.fromAddress().salt = salt;
    return idPreimage;
}

ContractIDPreimage
makeContractIDPreimage(Asset const& asset)
{
    ContractIDPreimage idPreimage(CONTRACT_ID_PREIMAGE_FROM_ASSET);
    idPreimage.fromAsset() = asset;
    return idPreimage;
}

HashIDPreimage
makeFullContractIdPreimage(Hash const& networkID,
                           ContractIDPreimage const& contractIDPreimage)
{
    HashIDPreimage fullPreImage;
    fullPreImage.type(ENVELOPE_TYPE_CONTRACT_ID);
    fullPreImage.contractID().contractIDPreimage = contractIDPreimage;
    fullPreImage.contractID().networkID = networkID;
    return fullPreImage;
}

ContractExecutable
makeWasmExecutable(Hash const& wasmHash)
{
    ContractExecutable executable(CONTRACT_EXECUTABLE_WASM);
    executable.wasm_hash() = wasmHash;
    return executable;
}

ContractExecutable
makeAssetExecutable(Asset const& asset)
{
    ContractExecutable executable(CONTRACT_EXECUTABLE_STELLAR_ASSET);
    return executable;
}

LedgerKey
makeContractInstanceKey(SCAddress const& contractAddress)
{
    SCVal instanceContractDataKey(SCValType::SCV_LEDGER_KEY_CONTRACT_INSTANCE);
    return contractDataKey(contractAddress, instanceContractDataKey,
                           CONTRACT_INSTANCE_ENTRY_DURABILITY);
}

TransactionFrameBasePtr
makeSorobanWasmUploadTx(Application& app, TestAccount& source,
                        RustBuf const& wasm, SorobanResources& uploadResources,
                        uint32_t inclusionFee)
{
    Operation uploadOp;
    uploadOp.body.type(INVOKE_HOST_FUNCTION);
    auto& uploadHF = uploadOp.body.invokeHostFunctionOp().hostFunction;
    uploadHF.type(HOST_FUNCTION_TYPE_UPLOAD_CONTRACT_WASM);
    uploadHF.wasm().assign(wasm.data.begin(), wasm.data.end());
    if (uploadResources.footprint.readWrite.empty())
    {
        REQUIRE(uploadResources.footprint.readOnly.empty());
        uploadResources.footprint.readWrite = {
            contractCodeKey(sha256(uploadHF.wasm()))};
    }
    auto uploadResourceFee =
        sorobanResourceFee(app, uploadResources, 1000 + wasm.data.size(), 40) +
        DEFAULT_TEST_RESOURCE_FEE;
    return sorobanTransactionFrameFromOps(app.getNetworkID(), source,
                                          {uploadOp}, {}, uploadResources,
                                          inclusionFee, uploadResourceFee);
}

SorobanResources
defaultUploadWasmResourcesWithoutFootprint(RustBuf const& wasm)
{
    SorobanResources resources;
    resources.instructions =
        static_cast<uint32_t>(500'000 + (wasm.data.size() * 1000));
    resources.readBytes = 1000;
    resources.writeBytes = static_cast<uint32_t>(wasm.data.size() + 100);
    return resources;
}

SorobanResources
defaultCreateWasmContractResources(RustBuf const& wasm)
{
    return SorobanResources();
}

TransactionFrameBasePtr
makeSorobanCreateContractTx(Application& app, TestAccount& source,
                            ContractIDPreimage const& idPreimage,
                            ContractExecutable const& executable,
                            SorobanResources& createResources,
                            uint32_t inclusionFee)
{
    if (createResources.footprint.readWrite.empty())
    {
        releaseAssert(createResources.footprint.readOnly.empty());
        auto contractID = xdrSha256(
            makeFullContractIdPreimage(app.getNetworkID(), idPreimage));
        if (executable.type() ==
            ContractExecutableType::CONTRACT_EXECUTABLE_WASM)
        {
            createResources.footprint.readOnly = {
                contractCodeKey(executable.wasm_hash())};
        }
        createResources.footprint.readWrite = {
            makeContractInstanceKey(makeContractAddress(contractID))};
    }

    Operation createOp;
    createOp.body.type(INVOKE_HOST_FUNCTION);
    auto& createHF = createOp.body.invokeHostFunctionOp().hostFunction;
    createHF.type(HOST_FUNCTION_TYPE_CREATE_CONTRACT);
    auto& createContractArgs = createHF.createContract();
    createContractArgs.contractIDPreimage = idPreimage;
    createContractArgs.executable = executable;

    if (executable.type() !=
        ContractExecutableType::CONTRACT_EXECUTABLE_STELLAR_ASSET)
    {
        SorobanAuthorizationEntry auth;
        auth.credentials.type(SOROBAN_CREDENTIALS_SOURCE_ACCOUNT);
        auth.rootInvocation.function.type(
            SOROBAN_AUTHORIZED_FUNCTION_TYPE_CREATE_CONTRACT_HOST_FN);
        auth.rootInvocation.function.createContractHostFn().contractIDPreimage =
            idPreimage;

        auth.rootInvocation.function.createContractHostFn().executable =
            executable;
        createOp.body.invokeHostFunctionOp().auth = {auth};
    }

    auto createResourceFee =
        sorobanResourceFee(app, createResources, 1000, 40) +
        DEFAULT_TEST_RESOURCE_FEE;
    return sorobanTransactionFrameFromOps(app.getNetworkID(), source,
                                          {createOp}, {}, createResources,
                                          inclusionFee, createResourceFee);
}

TransactionFrameBasePtr
sorobanTransactionFrameFromOps(Hash const& networkID, TestAccount& source,
                               std::vector<Operation> const& ops,
                               std::vector<SecretKey> const& opKeys,
                               SorobanInvocationSpec const& spec,
                               std::optional<std::string> memo,
                               std::optional<SequenceNumber> seq)
{
    return sorobanTransactionFrameFromOps(
        networkID, source, ops, opKeys, spec.getResources(),
        spec.getInclusionFee(), spec.getResourceFee());
}

SorobanInvocationSpec::SorobanInvocationSpec(SorobanResources const& resources,
                                             uint32_t nonRefundableResourceFee,
                                             uint32_t refundableResourceFee,
                                             uint32_t inclusionFee)
    : mResources(resources)
    , mNonRefundableResourceFee(nonRefundableResourceFee)
    , mRefundableResourceFee(refundableResourceFee)
    , mInclusionFee(inclusionFee)
{
}

SorobanResources const&
SorobanInvocationSpec::getResources() const
{
    return mResources;
}

uint32_t
SorobanInvocationSpec::getFee() const
{
    return mInclusionFee + getResourceFee();
}

uint32_t
SorobanInvocationSpec::getResourceFee() const
{
    return mNonRefundableResourceFee + mRefundableResourceFee;
}

uint32_t
SorobanInvocationSpec::getInclusionFee() const
{
    return mInclusionFee;
}

SorobanInvocationSpec
SorobanInvocationSpec::setInstructions(int64_t instructions) const
{
    auto newSpec = *this;
    newSpec.mResources.instructions = static_cast<uint32_t>(instructions);
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setReadOnlyFootprint(
    xdr::xvector<LedgerKey> const& keys) const
{
    auto newSpec = *this;
    newSpec.mResources.footprint.readOnly = keys;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setReadWriteFootprint(
    xdr::xvector<LedgerKey> const& keys) const
{
    auto newSpec = *this;
    newSpec.mResources.footprint.readWrite = keys;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::extendReadOnlyFootprint(
    xdr::xvector<LedgerKey> const& keys) const
{
    auto newSpec = *this;
    newSpec.mResources.footprint.readOnly.insert(
        newSpec.mResources.footprint.readOnly.end(), keys.begin(), keys.end());
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::extendReadWriteFootprint(
    xdr::xvector<LedgerKey> const& keys) const
{
    auto newSpec = *this;
    newSpec.mResources.footprint.readWrite.insert(
        newSpec.mResources.footprint.readWrite.end(), keys.begin(), keys.end());
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setReadBytes(uint32_t readBytes) const
{
    auto newSpec = *this;
    newSpec.mResources.readBytes = readBytes;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setWriteBytes(uint32_t writeBytes) const
{
    auto newSpec = *this;
    newSpec.mResources.writeBytes = writeBytes;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setRefundableResourceFee(uint32_t fee) const
{
    auto newSpec = *this;
    newSpec.mRefundableResourceFee = fee;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setNonRefundableResourceFee(uint32_t fee) const
{
    auto newSpec = *this;
    newSpec.mNonRefundableResourceFee = fee;
    return newSpec;
}

SorobanInvocationSpec
SorobanInvocationSpec::setInclusionFee(uint32_t fee) const
{
    auto newSpec = *this;
    newSpec.mInclusionFee = fee;
    return newSpec;
}

TestContract::TestContract(SorobanTest& test, SCAddress const& address,
                           xdr::xvector<LedgerKey> const& contractKeys)
    : mTest(test), mContractKeys(contractKeys), mAddress(address)
{
}

xdr::xvector<LedgerKey> const&
TestContract::getKeys() const
{
    return mContractKeys;
}

SCAddress const&
TestContract::getAddress() const
{
    return mAddress;
}

SorobanTest&
TestContract::getTest() const
{
    return mTest;
}

LedgerKey
TestContract::getDataKey(SCVal const& key, ContractDataDurability durability)
{
    return stellar::contractDataKey(mAddress, key, durability);
}

TestContract::Invocation
TestContract::prepareInvocation(std::string const& functionName,
                                std::vector<SCVal> const& args,
                                SorobanInvocationSpec const& spec,
                                bool addContractKeys) const
{
    return Invocation(*this, functionName, args, spec, addContractKeys);
}

void
TestContract::Invocation::deduplicateFootprint()
{
    xdr::xvector<LedgerKey> readOnly;
    xdr::xvector<LedgerKey> readWrite;
    UnorderedSet<LedgerKey> keys;

    auto deduplicate = [&](auto const& fp) {
        for (const auto& key : fp)
        {
            if (keys.insert(key).second)
            {
                readWrite.push_back(key);
            }
        }
    };
    deduplicate(mSpec.getResources().footprint.readWrite);
    deduplicate(mSpec.getResources().footprint.readOnly);
    mSpec =
        mSpec.setReadOnlyFootprint(readOnly).setReadWriteFootprint(readWrite);
}

TestContract::Invocation::Invocation(TestContract const& contract,
                                     std::string const& functionName,
                                     std::vector<SCVal> const& args,
                                     SorobanInvocationSpec const& spec,
                                     bool addContractKeys)
    : mTest(contract.getTest()), mSpec(spec)
{
    mOp.body.type(INVOKE_HOST_FUNCTION);
    auto& ihf = mOp.body.invokeHostFunctionOp().hostFunction;
    ihf.type(HOST_FUNCTION_TYPE_INVOKE_CONTRACT);
    ihf.invokeContract().contractAddress = contract.getAddress();
    ihf.invokeContract().functionName = makeSymbol(functionName);
    ihf.invokeContract().args.assign(args.begin(), args.end());

    if (addContractKeys)
    {
        mSpec = mSpec.extendReadOnlyFootprint(contract.getKeys());
    }
}

TestContract::Invocation&
TestContract::Invocation::withAuthorizedTopCall()
{
    SorobanAuthorizedInvocation ai;
    ai.function.type(SOROBAN_AUTHORIZED_FUNCTION_TYPE_CONTRACT_FN);
    ai.function.contractFn() =
        mOp.body.invokeHostFunctionOp().hostFunction.invokeContract();
    return withSourceAccountAuthorization(ai);
}

TestContract::Invocation&
TestContract::Invocation::withAuthorizedTopCall(SorobanSigner const& signer)
{
    SorobanAuthorizedInvocation ai;
    ai.function.type(SOROBAN_AUTHORIZED_FUNCTION_TYPE_CONTRACT_FN);
    ai.function.contractFn() =
        mOp.body.invokeHostFunctionOp().hostFunction.invokeContract();
    return withAuthorization(ai, signer);
}

TestContract::Invocation&
TestContract::Invocation::withAuthorization(
    SorobanAuthorizedInvocation const& invocation,
    SorobanCredentials credentials)
{
    mOp.body.invokeHostFunctionOp().auth.emplace_back(credentials, invocation);
    if (credentials.type() ==
        SorobanCredentialsType::SOROBAN_CREDENTIALS_ADDRESS)
    {
        SCVal nonceKey(SCValType::SCV_LEDGER_KEY_NONCE);
        nonceKey.nonce_key().nonce = credentials.address().nonce;
        mSpec = mSpec.extendReadWriteFootprint(
            {contractDataKey(credentials.address().address, nonceKey,
                             ContractDataDurability::TEMPORARY)});
    }
    return *this;
}

TestContract::Invocation&
TestContract::Invocation::withAuthorization(
    SorobanAuthorizedInvocation const& invocation, SorobanSigner const& signer)
{
    mSpec = mSpec.extendReadOnlyFootprint(signer.getLedgerKeys());
    return withAuthorization(invocation, signer.sign(invocation));
}

TestContract::Invocation&
TestContract::Invocation::withSourceAccountAuthorization(
    SorobanAuthorizedInvocation const& invocation)
{
    SorobanCredentials credentials(SOROBAN_CREDENTIALS_SOURCE_ACCOUNT);
    return withAuthorization(invocation, credentials);
}

TestContract::Invocation&
TestContract::Invocation::withDeduplicatedFootprint()
{
    mDeduplicateFootprint = true;
    return *this;
}

TestContract::Invocation&
TestContract::Invocation::withSpec(SorobanInvocationSpec const& spec)
{
    mSpec = spec;
    return *this;
}

SorobanInvocationSpec
TestContract::Invocation::getSpec()
{
    return mSpec;
}

TransactionFrameBasePtr
TestContract::Invocation::createTx(TestAccount* source)
{
    if (mDeduplicateFootprint)
    {
        deduplicateFootprint();
    }
    auto& acc = source ? *source : mTest.getRoot();

    return sorobanTransactionFrameFromOps(mTest.getApp().getNetworkID(), acc,
                                          {mOp}, {}, mSpec);
}

TestContract::Invocation&
TestContract::Invocation::withExactNonRefundableResourceFee()
{
    // Compute tx frame size before finalizing the resource fee via a dummy TX.
    // This is a bit hacky, but we need the exact tx size in order to
    // enable tests that rely on the exact refundable fee value.
    // Note, that we don't use the root account here in order to not mess up
    // the sequence numbers.
    auto dummyTx = sorobanTransactionFrameFromOps(mTest.getApp().getNetworkID(),
                                                  mTest.getDummyAccount(),
                                                  {mOp}, {}, mSpec);
    auto txSize = xdr::xdr_size(dummyTx->getEnvelope());
    auto fee =
        sorobanResourceFee(mTest.getApp(), mSpec.getResources(), txSize, 0);
    releaseAssert(fee <= UINT32_MAX);
    mSpec = mSpec.setNonRefundableResourceFee(static_cast<uint32_t>(fee));
    return *this;
}

bool
TestContract::Invocation::invoke(TestAccount* source)
{
    auto tx = createTx(source);
    CLOG_INFO(Tx, "invoke tx {}", xdr::xdr_to_string(tx->getEnvelope()));
    mTxMeta.emplace(mTest.getLedgerVersion());
    bool success = mTest.invokeTx(tx, &*mTxMeta);
    if (tx->getResult().result.code() == txFAILED ||
        tx->getResult().result.code() == txSUCCESS)
    {
        mResultCode = tx->getResult()
                          .result.results()[0]
                          .tr()
                          .invokeHostFunctionResult()
                          .code();
    }
    else
    {
        mResultCode = std::nullopt;
    }
    return success;
}

SCVal
TestContract::Invocation::getReturnValue() const
{
    REQUIRE(mTxMeta);
    return mTxMeta->getXDR().v3().sorobanMeta->returnValue;
}

TransactionMetaFrame const&
TestContract::Invocation::getTxMeta() const
{
    REQUIRE(mTxMeta);
    return *mTxMeta;
}

std::optional<InvokeHostFunctionResultCode>
TestContract::Invocation::getResultCode() const
{
    return mResultCode;
}

SorobanTest::SorobanTest(Config cfg, bool useTestLimits,
                         std::function<void(SorobanNetworkConfig&)> cfgModifyFn)
    : mApp(createTestApplication(mClock, cfg))
    , mRoot(TestAccount::createRoot(getApp()))
    , mDummyAccount(mRoot.create(
          "dummyAcc", getApp().getLedgerManager().getLastMinBalance(1)))
{
    if (useTestLimits)
    {
        overrideSorobanNetworkConfigForTest(getApp());
    }

    modifySorobanNetworkConfig(getApp(), cfgModifyFn);
}

int64_t
SorobanTest::computeFeePerIncrement(int64_t resourceVal, int64_t feeRate,
                                    int64_t increment)
{
    // ceiling division for (resourceVal * feeRate) / increment
    int64_t num = (resourceVal * feeRate);
    return (num + increment - 1) / increment;
};

void
SorobanTest::invokeArchivalOp(TransactionFrameBasePtr tx,
                              int64_t expectedRefundableFeeCharged)
{
    REQUIRE(isTxValid(tx));
    int64_t initBalance = getRoot().getBalance();

    // Initially we store in result the charge for resources plus
    // minimum inclusion  fee bid (currently equivalent to the network
    // `baseFee` of 100).
    int64_t baseCharged = (tx->getFullFee() - tx->getInclusionFee()) + 100;
    REQUIRE(tx->getResult().feeCharged == baseCharged);
    // Charge the fee.
    {
        LedgerTxn ltx(getApp().getLedgerTxnRoot());
        // Imitate surge pricing by charging at a higher rate than base
        // fee.
        tx->processFeeSeqNum(ltx, 300);
        ltx.commit();
    }
    // The resource and the base fee are charged, with additional
    // surge pricing fee.
    int64_t balanceAfterFeeCharged = getRoot().getBalance();
    auto actuallyCharged = baseCharged + /* surge pricing additional fee */ 200;
    REQUIRE(initBalance - balanceAfterFeeCharged == actuallyCharged);
    auto nonRefundableResourceFee = sorobanResourceFee(
        getApp(), tx->sorobanResources(), xdr::xdr_size(tx->getEnvelope()), 0);
    auto expectedChargedAfterRefund =
        nonRefundableResourceFee + expectedRefundableFeeCharged + 300;

    TransactionMetaFrame txm(getLedgerVersion());
    REQUIRE(invokeTx(tx, &txm));
    {
        LedgerTxn ltx(getApp().getLedgerTxnRoot());
        tx->processPostApply(getApp(), ltx, txm);
        ltx.commit();
    }

    auto changesAfter = txm.getChangesAfter();
    REQUIRE(changesAfter.size() == 2);
    REQUIRE(changesAfter[1].updated().data.account().balance -
                changesAfter[0].state().data.account().balance ==
            actuallyCharged - expectedChargedAfterRefund);

    // The account should receive a refund for unspent refundable fee.
    REQUIRE(getRoot().getBalance() - balanceAfterFeeCharged ==
            actuallyCharged - expectedChargedAfterRefund);
}

Hash
SorobanTest::uploadWasm(RustBuf const& wasm, SorobanResources& uploadResources)
{
    auto tx =
        makeSorobanWasmUploadTx(getApp(), mRoot, wasm, uploadResources, 1000);
    REQUIRE(invokeTx(tx));

    // Verify the uploaded contract code is correct.
    SCBytes expectedWasm(wasm.data.begin(), wasm.data.end());
    Hash expectedWasmHash = sha256(expectedWasm);
    auto contractCodeLedgerKey = contractCodeKey(expectedWasmHash);
    {
        LedgerTxn ltx(getApp().getLedgerTxnRoot());
        auto ltxe = ltx.loadWithoutRecord(contractCodeLedgerKey);
        REQUIRE(ltxe);
        REQUIRE(ltxe.current().data.contractCode().code == expectedWasm);
    }
    // Check TTLs for contract code
    auto expectedLiveUntilLedger =
        getNetworkCfg().stateArchivalSettings().minPersistentTTL +
        getLedgerSeq() - 1;
    REQUIRE(getTTL(contractCodeLedgerKey) == expectedLiveUntilLedger);
    return expectedWasmHash;
}

SCAddress
SorobanTest::createContract(ContractIDPreimage const& idPreimage,
                            ContractExecutable const& executable,
                            SorobanResources& createResources)
{
    auto tx = makeSorobanCreateContractTx(getApp(), mRoot, idPreimage,
                                          executable, createResources, 1000);
    REQUIRE(invokeTx(tx));
    Hash contractID = xdrSha256(
        makeFullContractIdPreimage(getApp().getNetworkID(), idPreimage));
    SCAddress contractAddress = makeContractAddress(contractID);
    auto contractInstanceKey = makeContractInstanceKey(contractAddress);
    {
        // Verify the created instance.
        LedgerTxn ltx(getApp().getLedgerTxnRoot());
        auto ltxe = ltx.load(contractInstanceKey);
        REQUIRE(ltxe);

        auto const& cd = ltxe.current().data.contractData();
        REQUIRE(cd.durability == CONTRACT_INSTANCE_ENTRY_DURABILITY);
        REQUIRE(cd.val.instance().executable == executable);
    }

    // Check TTLs for the contract instance.
    auto const& ses = getNetworkCfg().stateArchivalSettings();
    auto expectedLiveUntilLedger = ses.minPersistentTTL + getLedgerSeq() - 1;
    REQUIRE(getTTL(contractInstanceKey) == expectedLiveUntilLedger);
    return contractAddress;
}

int64_t
SorobanTest::getRentFeeForExtension(xdr::xvector<LedgerKey> const& keys,
                                    uint32_t newLifetime)

{
    rust::Vec<CxxLedgerEntryRentChange> rustEntryRentChanges;
    LedgerTxn ltx(getApp().getLedgerTxnRoot());
    for (auto const& key : keys)
    {
        auto ltxe = ltx.loadWithoutRecord(key);
        REQUIRE(ltxe);
        size_t entrySize = xdr::xdr_size(ltxe.current());
        rustEntryRentChanges.emplace_back();
        auto& rustChange = rustEntryRentChanges.back();
        rustChange.is_persistent = !isTemporaryEntry(key);
        rustChange.old_size_bytes = static_cast<uint32>(entrySize);
        rustChange.new_size_bytes = rustChange.old_size_bytes;
        auto ttlLtxe = ltx.loadWithoutRecord(getTTLKey(key));
        REQUIRE(ttlLtxe);
        rustChange.old_live_until_ledger =
            ttlLtxe.current().data.ttl().liveUntilLedgerSeq;
        rustChange.new_live_until_ledger = getLedgerSeq() + newLifetime;
    }
    return rust_bridge::compute_rent_fee(
        getApp().getConfig().CURRENT_LEDGER_PROTOCOL_VERSION,
        getLedgerVersion(), rustEntryRentChanges,
        getNetworkCfg().rustBridgeRentFeeConfiguration(), getLedgerSeq());
}

Application&
SorobanTest::getApp() const
{
    return *mApp;
}

TestContract&
SorobanTest::deployWasmContract(RustBuf const& wasm,
                                std::optional<SorobanResources> uploadResources,
                                std::optional<SorobanResources> createResources)
{
    if (!uploadResources)
    {
        uploadResources = defaultUploadWasmResourcesWithoutFootprint(wasm);
    }

    if (!createResources)
    {
        createResources.emplace();
        createResources->instructions = 5'000'000;
        createResources->readBytes =
            static_cast<uint32_t>(wasm.data.size() + 1000);
        createResources->writeBytes = 1000;
    }
    Hash wasmHash = uploadWasm(wasm, *uploadResources);
    SCAddress contractAddress =
        createContract(makeContractIDPreimage(
                           mRoot, sha256(std::to_string(mContracts.size()))),
                       makeWasmExecutable(wasmHash), *createResources);
    xdr::xvector<LedgerKey> contractKeys = {
        contractCodeKey(wasmHash), makeContractInstanceKey(contractAddress)};
    mContracts.emplace_back(
        std::make_unique<TestContract>(*this, contractAddress, contractKeys));
    return *mContracts.back();
}

TestContract&
SorobanTest::deployAssetContract(Asset const& asset)
{
    SorobanResources createResources;
    createResources.instructions = 400'000;
    createResources.readBytes = 1000;
    createResources.writeBytes = 1000;

    SCAddress contractAddress =
        createContract(makeContractIDPreimage(asset),
                       makeAssetExecutable(asset), createResources);
    xdr::xvector<LedgerKey> contractKeys = {
        makeContractInstanceKey(contractAddress)};
    mContracts.emplace_back(
        std::make_unique<TestContract>(*this, contractAddress, contractKeys));
    return *mContracts.back();
}

TestAccount&
SorobanTest::getRoot()
{
    // TestAccount caches the next seqno in-memory, assuming all invoked TXs
    // succeed. This is not true for these tests, so we load the seqno from
    // disk to circumvent the cache.
    mRoot.loadSequenceNumber();
    return mRoot;
}

TestAccount&
SorobanTest::getDummyAccount()
{
    return mDummyAccount;
}

SorobanNetworkConfig const&
SorobanTest::getNetworkCfg()
{
    return getApp().getLedgerManager().getSorobanNetworkConfig();
}

uint32_t
SorobanTest::getLedgerVersion() const
{
    return getApp()
        .getLedgerManager()
        .getLastClosedLedgerHeader()
        .header.ledgerVersion;
}

uint32_t
SorobanTest::getLedgerSeq() const
{
    return getApp().getLedgerManager().getLastClosedLedgerNum();
}

TransactionFrameBasePtr
SorobanTest::createExtendOpTx(SorobanResources const& resources,
                              uint32_t extendTo, uint32_t fee,
                              int64_t refundableFee, TestAccount* source)
{
    Operation op;
    op.body.type(EXTEND_FOOTPRINT_TTL);
    op.body.extendFootprintTTLOp().extendTo = extendTo;
    auto& acc = source ? *source : getRoot();
    return sorobanTransactionFrameFromOps(getApp().getNetworkID(), acc, {op},
                                          {}, resources, fee, refundableFee);
}

TransactionFrameBasePtr
SorobanTest::createRestoreTx(SorobanResources const& resources, uint32_t fee,
                             int64_t refundableFee, TestAccount* source)
{
    Operation op;
    op.body.type(RESTORE_FOOTPRINT);
    auto& acc = source ? *source : getRoot();
    return sorobanTransactionFrameFromOps(getApp().getNetworkID(), acc, {op},
                                          {}, resources, fee, refundableFee);
}

bool
SorobanTest::isTxValid(TransactionFrameBasePtr tx)
{
    LedgerTxn ltx(getApp().getLedgerTxnRoot());
    auto ret = tx->checkValid(getApp(), ltx, 0, 0, 0);
    return ret;
}

bool
SorobanTest::invokeTx(TransactionFrameBasePtr tx, TransactionMetaFrame* txMeta)
{
    LedgerTxn ltx(getApp().getLedgerTxnRoot());
    TransactionMetaFrame dummyMeta(getLedgerVersion());
    if (txMeta == nullptr)
    {
        txMeta = &dummyMeta;
    }
    REQUIRE(tx->checkValid(getApp(), ltx, 0, 0, 0));
    bool res = tx->apply(getApp(), ltx, *txMeta);
    ltx.commit();
    return res;
}

uint32_t
SorobanTest::getTTL(LedgerKey const& k)
{
    LedgerTxn ltx(getApp().getLedgerTxnRoot());
    auto ltxe = ltx.loadWithoutRecord(k);
    REQUIRE(ltxe);

    auto ttlKey = getTTLKey(k);
    auto ttlLtxe = ltx.loadWithoutRecord(ttlKey);
    REQUIRE(ttlLtxe);
    return ttlLtxe.current().data.ttl().liveUntilLedgerSeq;
}

bool
SorobanTest::isEntryLive(LedgerKey const& k, uint32_t ledgerSeq)
{
    auto ttlKey = getTTLKey(k);
    LedgerTxn ltx(getApp().getLedgerTxnRoot());
    auto ttlLtxe = ltx.loadWithoutRecord(ttlKey);
    REQUIRE(ttlLtxe);
    return isLive(ttlLtxe.current(), ledgerSeq);
}

void
SorobanTest::invokeRestoreOp(xdr::xvector<LedgerKey> const& readWrite,
                             int64_t expectedRefundableFeeCharged)
{
    SorobanResources resources;
    resources.footprint.readWrite = readWrite;
    resources.instructions = 0;
    resources.readBytes = 10'000;
    resources.writeBytes = 10'000;

    auto resourceFee = 300'000 + 40'000 * readWrite.size();
    auto tx = createRestoreTx(resources, 1'000, resourceFee);
    invokeArchivalOp(tx, expectedRefundableFeeCharged);
}

void
SorobanTest::invokeExtendOp(xdr::xvector<LedgerKey> const& readOnly,
                            uint32_t extendTo,
                            std::optional<int64_t> expectedRefundableFeeCharged)
{
    if (!expectedRefundableFeeCharged)
    {
        expectedRefundableFeeCharged =
            getRentFeeForExtension(readOnly, extendTo);
    }

    SorobanResources extendResources;
    extendResources.footprint.readOnly = readOnly;
    extendResources.readBytes = 10'000;

    auto resourceFee = DEFAULT_TEST_RESOURCE_FEE * readOnly.size();
    auto tx = createExtendOpTx(extendResources, extendTo, 1'000, resourceFee);
    invokeArchivalOp(tx, *expectedRefundableFeeCharged);
}

SorobanSigner
SorobanTest::createContractSigner(TestContract const& contract,
                                  std::function<SCVal(uint256)> signFn)
{
    return SorobanSigner(*this, contract.getAddress(), contract.getKeys(),
                         signFn);
}

SorobanSigner
SorobanTest::createClassicAccountSigner(TestAccount const& account,
                                        std::vector<TestAccount*> signers)
{
    xdr::xvector<LedgerKey> accountKeys;
    // Account key has to be already present in the footprint, but the account
    // owner doesn't have to be the signer, so add the account key to the
    // footprint unconditionally.
    LedgerKey accountKey(LedgerEntryType::ACCOUNT);
    accountKey.account().accountID = account.getPublicKey();
    accountKeys.push_back(accountKey);
    for (const auto& signer : signers)
    {
        if (signer->getPublicKey().ed25519() !=
            account.getPublicKey().ed25519())
        {
            LedgerKey signerKey(LedgerEntryType::ACCOUNT);
            signerKey.account().accountID = signer->getPublicKey();
            accountKeys.push_back(signerKey);
        }
    }
    return SorobanSigner(*this, makeAccountAddress(account.getPublicKey()),
                         {accountKey}, [signers](uint256 payload) {
                             return signPayloadForClassicAccount(signers,
                                                                 payload);
                         });
}

AssetContractTestClient::AssetContractTestClient(SorobanTest& test,
                                                 Asset const& asset)
    : mContract(test.deployAssetContract(asset))
    , mAsset(asset)
    , mApp(mContract.getTest().getApp())
{
}

LedgerKey
AssetContractTestClient::makeBalanceKey(AccountID const& acc)
{
    LedgerKey balanceKey;
    if (mAsset.type() == ASSET_TYPE_NATIVE)
    {
        balanceKey.type(ACCOUNT);
        balanceKey.account().accountID = acc;
    }
    else
    {
        balanceKey.type(TRUSTLINE);
        balanceKey.trustLine().accountID = acc;
        balanceKey.trustLine().asset = assetToTrustLineAsset(mAsset);
    }

    return balanceKey;
}

LedgerKey
AssetContractTestClient::makeContractDataBalanceKey(SCAddress const& addr)
{
    SCVal val(SCV_ADDRESS);
    val.address() = addr;

    LedgerKey balanceKey(CONTRACT_DATA);
    balanceKey.contractData().contract = mContract.getAddress();

    balanceKey.contractData().key =
        makeVecSCVal({makeSymbolSCVal("Balance"), val});
    balanceKey.contractData().durability = ContractDataDurability::PERSISTENT;

    return balanceKey;
}

LedgerKey
AssetContractTestClient::makeBalanceKey(SCAddress const& addr)
{
    if (addr.type() == SC_ADDRESS_TYPE_ACCOUNT)
    {
        return makeBalanceKey(addr.accountId());
    }
    else
    {
        return makeContractDataBalanceKey(addr);
    }
}

LedgerKey
AssetContractTestClient::makeIssuerKey(Asset const& asset)
{
    LedgerKey issuerLedgerKey(ACCOUNT);
    issuerLedgerKey.account().accountID = getIssuer(asset);
    return issuerLedgerKey;
}

int64_t
AssetContractTestClient::getBalance(SCAddress const& addr)
{
    SCVal val(SCV_ADDRESS);
    val.address() = addr;

    return addr.type() == SC_ADDRESS_TYPE_ACCOUNT
               ? txtest::getBalance(mApp, addr.accountId(), mAsset)
               : getContractBalance(mApp, mContract.getAddress(), val);
}

SorobanInvocationSpec
AssetContractTestClient::defaultSpec() const
{
    return SorobanInvocationSpec()
        .setInstructions(2'000'000)
        .setReadBytes(2000)
        .setWriteBytes(2000);
}

TestContract const&
AssetContractTestClient::getContract() const
{
    return mContract;
}

bool
AssetContractTestClient::transfer(TestAccount& fromAcc, SCAddress const& toAddr,
                                  int64_t amount)
{
    SCVal toVal(SCV_ADDRESS);
    toVal.address() = toAddr;

    SCVal fromVal(SCV_ADDRESS);
    fromVal.address() = makeAccountAddress(fromAcc.getPublicKey());

    LedgerKey fromBalanceKey = makeBalanceKey(fromAcc.getPublicKey());
    LedgerKey toBalanceKey = makeBalanceKey(toAddr);

    auto spec = defaultSpec();

    bool fromIsIssuer = false;
    bool toIsIssuer = false;
    if (mAsset.type() != ASSET_TYPE_NATIVE)
    {
        spec = spec.extendReadOnlyFootprint({makeIssuerKey(mAsset)});

        if (getIssuer(mAsset) == fromAcc.getPublicKey())
        {
            fromIsIssuer = true;
        }
        else
        {
            spec = spec.extendReadWriteFootprint({fromBalanceKey});
        }

        if (toAddr.type() == SC_ADDRESS_TYPE_ACCOUNT &&
            getIssuer(mAsset) == toAddr.accountId())
        {
            toIsIssuer = true;
        }
        else
        {
            spec = spec.extendReadWriteFootprint({toBalanceKey});
        }
    }
    else
    {
        spec = spec.setReadWriteFootprint({fromBalanceKey, toBalanceKey});
    }

    auto preTransferFromBalance = mAsset.type() == ASSET_TYPE_NATIVE
                                      ? fromAcc.getBalance()
                                      : fromAcc.getTrustlineBalance(mAsset);
    auto preTransferToBalance = getBalance(toAddr);
    auto invocation =
        mContract
            .prepareInvocation("transfer", {fromVal, toVal, makeI128(amount)},
                               spec)
            .withAuthorizedTopCall();
    bool success = invocation.invoke(&fromAcc);
    auto postTransferFromBalance = mAsset.type() == ASSET_TYPE_NATIVE
                                       ? fromAcc.getBalance()
                                       : fromAcc.getTrustlineBalance(mAsset);

    auto postTransferToBalance = getBalance(toAddr);
    if (success)
    {
        REQUIRE((fromIsIssuer ||
                 postTransferFromBalance == preTransferFromBalance - amount));
        REQUIRE((toIsIssuer ||
                 postTransferToBalance - amount == preTransferToBalance));

        {
            // From is an account so it should never have a contract data
            // balance
            LedgerTxn ltx(mApp.getLedgerTxnRoot());
            REQUIRE(!ltx.load(makeContractDataBalanceKey(fromVal.address())));
        }

        if (toIsIssuer)
        {
            // make sure we didn't create an entry for the issuer
            LedgerTxn ltx(mApp.getLedgerTxnRoot());
            REQUIRE(!ltx.load(makeContractDataBalanceKey(toAddr)));
        }
    }
    else
    {
        if (mApp.getConfig().ENABLE_SOROBAN_DIAGNOSTIC_EVENTS)
        {
            // Check for a contract error in the second event (the first should
            // be an `fn_call` event)
            auto const& sorobanMeta =
                invocation.getTxMeta().getXDR().v3().sorobanMeta;
            REQUIRE(sorobanMeta->events.size() == 0);
            REQUIRE(sorobanMeta->diagnosticEvents.size() > 1);

            auto const& contract_ev = sorobanMeta->diagnosticEvents.at(1);
            REQUIRE(!contract_ev.inSuccessfulContractCall);
            REQUIRE(contract_ev.event.type == ContractEventType::DIAGNOSTIC);
            auto const& topics = contract_ev.event.body.v0().topics.at(1);
            REQUIRE(topics.type() == SCV_ERROR);
            REQUIRE(topics.error().type() == SCE_CONTRACT);
        }

        REQUIRE(postTransferFromBalance == preTransferFromBalance);
        REQUIRE(postTransferToBalance == preTransferToBalance);
    }
    return success;
}

bool
AssetContractTestClient::mint(TestAccount& admin, SCAddress const& toAddr,
                              int64_t amount)
{
    SCVal toVal(SCV_ADDRESS);
    toVal.address() = toAddr;

    LedgerKey toBalanceKey = makeBalanceKey(toAddr);

    auto spec = defaultSpec().setReadWriteFootprint({toBalanceKey});

    if (mAsset.type() != ASSET_TYPE_NATIVE)
    {
        LedgerKey issuerLedgerKey(ACCOUNT);
        issuerLedgerKey.account().accountID = getIssuer(mAsset);
        spec = spec.extendReadOnlyFootprint({issuerLedgerKey});
    }

    auto preMintBalance = getBalance(toAddr);

    bool success =
        mContract.prepareInvocation("mint", {toVal, makeI128(amount)}, spec)
            .withAuthorizedTopCall()
            .invoke(&admin);
    auto postMintBalance = getBalance(toAddr);
    if (success)
    {
        REQUIRE(postMintBalance - amount == preMintBalance);
    }
    else
    {
        REQUIRE(postMintBalance == preMintBalance);
    }
    return success;
}

bool
AssetContractTestClient::burn(TestAccount& from, int64_t amount)
{
    auto fromAddr = makeAccountAddress(from.getPublicKey());

    SCVal fromVal(SCV_ADDRESS);
    fromVal.address() = fromAddr;

    LedgerKey fromBalanceKey;
    if (mAsset.type() == ASSET_TYPE_NATIVE)
    {
        fromBalanceKey.type(ACCOUNT);
        fromBalanceKey.account().accountID = fromAddr.accountId();
    }
    else
    {
        fromBalanceKey.type(TRUSTLINE);
        fromBalanceKey.trustLine().accountID = fromAddr.accountId();
        fromBalanceKey.trustLine().asset = assetToTrustLineAsset(mAsset);
    }

    auto spec = defaultSpec();

    bool isIssuer = getIssuer(mAsset) == from.getPublicKey();
    if (!isIssuer)
    {
        spec = spec.extendReadWriteFootprint({fromBalanceKey});
    }

    auto preBurnBalance = getBalance(fromAddr);

    bool success =
        mContract.prepareInvocation("burn", {fromVal, makeI128(amount)}, spec)
            .withAuthorizedTopCall()
            .invoke(&from);
    auto postBurnBalance = getBalance(fromAddr);
    if (success)
    {
        REQUIRE(preBurnBalance - amount == postBurnBalance);
    }
    else
    {
        REQUIRE(preBurnBalance == postBurnBalance);
    }
    return success;
}

bool
AssetContractTestClient::clawback(TestAccount& admin, SCAddress const& fromAddr,
                                  int64_t amount)
{
    SCVal fromVal(SCV_ADDRESS);
    fromVal.address() = fromAddr;

    LedgerKey fromBalanceKey = makeBalanceKey(fromAddr);
    auto spec = defaultSpec().setReadWriteFootprint({fromBalanceKey});

    if (mAsset.type() != ASSET_TYPE_NATIVE)
    {
        spec = spec.extendReadOnlyFootprint({makeIssuerKey(mAsset)});
    }

    auto preClawbackBalance = getBalance(fromAddr);

    bool success =
        mContract
            .prepareInvocation("clawback", {fromVal, makeI128(amount)}, spec)
            .withAuthorizedTopCall()
            .invoke(&admin);
    auto postClawbackBalance = getBalance(fromAddr);
    if (success)
    {
        REQUIRE(preClawbackBalance - amount == postClawbackBalance);
    }
    else
    {
        REQUIRE(preClawbackBalance == postClawbackBalance);
    }
    return success;
}

ContractStorageTestClient::ContractStorageTestClient(SorobanTest& test)
    : mContract(
          test.deployWasmContract(rust_bridge::get_test_wasm_contract_data()))
{
}

TestContract&
ContractStorageTestClient::getContract() const
{
    return mContract;
}

SorobanInvocationSpec
ContractStorageTestClient::defaultSpecWithoutFootprint() const
{
    return SorobanInvocationSpec()
        .setInstructions(4'000'000)
        .setReadBytes(10'000)
        .setNonRefundableResourceFee(0)
        .setRefundableResourceFee(40'000);
}

SorobanInvocationSpec
ContractStorageTestClient::readKeySpec(std::string const& key,
                                       ContractDataDurability durability) const
{
    return defaultSpecWithoutFootprint().setReadOnlyFootprint(
        {mContract.getDataKey(makeSymbolSCVal(key), durability)});
}

SorobanInvocationSpec
ContractStorageTestClient::writeKeySpec(std::string const& key,
                                        ContractDataDurability durability) const
{
    return defaultSpecWithoutFootprint()
        .setReadWriteFootprint(
            {mContract.getDataKey(makeSymbolSCVal(key), durability)})
        .setWriteBytes(1000);
}

uint32_t
ContractStorageTestClient::getTTL(std::string const& key,
                                  ContractDataDurability durability)
{
    return mContract.getTest().getTTL(
        mContract.getDataKey(makeSymbolSCVal(key), durability));
}

bool
ContractStorageTestClient::isEntryLive(std::string const& key,
                                       ContractDataDurability durability,
                                       uint32_t ledgerSeq)
{
    return mContract.getTest().isEntryLive(
        mContract.getDataKey(makeSymbolSCVal(key), durability), ledgerSeq);
}

InvokeHostFunctionResultCode
ContractStorageTestClient::put(std::string const& key,
                               ContractDataDurability durability, uint64_t val,
                               std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        spec = writeKeySpec(key, durability);
    }

    std::string funcStr = durability == ContractDataDurability::TEMPORARY
                              ? "put_temporary"
                              : "put_persistent";
    auto invocation = mContract.prepareInvocation(
        funcStr, {makeSymbolSCVal(key), makeU64SCVal(val)}, *spec);
    invocation.withExactNonRefundableResourceFee().invoke();
    return *invocation.getResultCode();
}

InvokeHostFunctionResultCode
ContractStorageTestClient::get(std::string const& key,
                               ContractDataDurability durability,
                               std::optional<uint64_t> expectValue,
                               std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        spec = readKeySpec(key, durability);
    }

    std::string funcStr = durability == ContractDataDurability::TEMPORARY
                              ? "get_temporary"
                              : "get_persistent";
    auto invocation =
        mContract.prepareInvocation(funcStr, {makeSymbolSCVal(key)}, *spec);
    bool isSuccess = invocation.withExactNonRefundableResourceFee().invoke();
    if (isSuccess && expectValue)
    {
        REQUIRE(*expectValue == invocation.getReturnValue().u64());
    }
    return *invocation.getResultCode();
}

InvokeHostFunctionResultCode
ContractStorageTestClient::has(std::string const& key,
                               ContractDataDurability durability,
                               std::optional<bool> expectHas,
                               std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        spec = readKeySpec(key, durability);
    }

    std::string funcStr = durability == ContractDataDurability::TEMPORARY
                              ? "has_temporary"
                              : "has_persistent";

    auto invocation =
        mContract.prepareInvocation(funcStr, {makeSymbolSCVal(key)}, *spec);
    bool isSuccess = invocation.withExactNonRefundableResourceFee().invoke();
    if (isSuccess && expectHas)
    {
        REQUIRE(invocation.getReturnValue().b() == expectHas);
    }
    return *invocation.getResultCode();
}

InvokeHostFunctionResultCode
ContractStorageTestClient::del(std::string const& key,
                               ContractDataDurability durability,
                               std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        // Even though deletion is technically a write, we don't charge any
        // bytes for it and thus don't need to add write bytes to resources.
        spec = writeKeySpec(key, durability).setWriteBytes(0);
    }

    std::string funcStr = durability == ContractDataDurability::TEMPORARY
                              ? "del_temporary"
                              : "del_persistent";

    auto invocation =
        mContract.prepareInvocation(funcStr, {makeSymbolSCVal(key)}, *spec);
    invocation.withExactNonRefundableResourceFee().invoke();
    return *invocation.getResultCode();
}

InvokeHostFunctionResultCode
ContractStorageTestClient::extend(std::string const& key,
                                  ContractDataDurability durability,
                                  uint32_t threshold, uint32_t extendTo,
                                  std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        spec = readKeySpec(key, durability);
    }

    std::string funcStr = durability == ContractDataDurability::TEMPORARY
                              ? "extend_temporary"
                              : "extend_persistent";
    auto invocation = mContract.prepareInvocation(
        funcStr, {makeSymbolSCVal(key), makeU32(threshold), makeU32(extendTo)},
        *spec);
    invocation.withExactNonRefundableResourceFee().invoke();
    return *invocation.getResultCode();
}

InvokeHostFunctionResultCode
ContractStorageTestClient::resizeStorageAndExtend(
    std::string const& key, uint32_t numKiloBytes, uint32_t thresh,
    uint32_t extendTo, std::optional<SorobanInvocationSpec> spec)
{
    if (!spec)
    {
        spec = writeKeySpec(key, ContractDataDurability::PERSISTENT)
                   .setWriteBytes(numKiloBytes * 1024 + 200);
    }

    std::string funcStr = "replace_with_bytes_and_extend";
    auto invocation = mContract.prepareInvocation(
        funcStr,
        {makeSymbolSCVal(key), makeU32(numKiloBytes), makeU32(thresh),
         makeU32(extendTo)},
        *spec);
    invocation.withExactNonRefundableResourceFee().invoke();
    return *invocation.getResultCode();
}

SorobanSigner::SorobanSigner(SorobanTest& test, SCAddress const& address,
                             xdr::xvector<LedgerKey> const& keys,
                             std::function<SCVal(uint256)> signFn)
    : mTest(test), mSignFn(signFn), mAddress(address), mKeys(keys)
{
}

SorobanCredentials
SorobanSigner::sign(SorobanAuthorizedInvocation const& invocation) const
{
    SorobanCredentials fullCredentials(
        SorobanCredentialsType::SOROBAN_CREDENTIALS_ADDRESS);
    auto& credentials = fullCredentials.address();
    credentials.nonce = uniform_int_distribution<int64_t>()(Catch::rng());
    credentials.signatureExpirationLedger = mTest.getLedgerSeq() + 10'000;
    credentials.address = mAddress;

    HashIDPreimage signaturePreimage(
        EnvelopeType::ENVELOPE_TYPE_SOROBAN_AUTHORIZATION);
    auto& preimage = signaturePreimage.sorobanAuthorization();
    preimage.invocation = invocation;
    preimage.networkID = mTest.getApp().getNetworkID();
    preimage.nonce = credentials.nonce;
    preimage.signatureExpirationLedger = credentials.signatureExpirationLedger;

    credentials.signature = mSignFn(xdrSha256(signaturePreimage));
    return fullCredentials;
}

SCVal
SorobanSigner::getAddressVal() const
{
    return makeAddressSCVal(mAddress);
}

xdr::xvector<LedgerKey> const&
SorobanSigner::getLedgerKeys() const
{
    return mKeys;
}

AuthTestTreeNode::AuthTestTreeNode(SCAddress const& contract)
    : mContractAddress(contract)
{
}

AuthTestTreeNode&
AuthTestTreeNode::add(std::vector<AuthTestTreeNode> children)
{
    mChildren.insert(mChildren.end(), children.begin(), children.end());
    return *this;
}

void
AuthTestTreeNode::setAddress(SCAddress const& address)
{
    mContractAddress = address;
}

SCVal
AuthTestTreeNode::toSCVal(int addressCount) const
{
    std::vector<SCVal> children;
    for (auto const& child : mChildren)
    {
        children.emplace_back(child.toSCVal(addressCount));
    }

    SCVal node(SCV_MAP);
    auto& fields = node.map().activate();

    fields.emplace_back(makeSymbolSCVal("children"), makeVecSCVal(children));
    fields.emplace_back(makeSymbolSCVal("contract"),
                        makeAddressSCVal(mContractAddress));
    fields.emplace_back(
        makeSymbolSCVal("need_auth"),
        makeVecSCVal(std::vector<SCVal>(addressCount, makeBool(true))));
    fields.emplace_back(makeSymbolSCVal("try_call"), makeBool(false));
    return node;
}

SorobanAuthorizedInvocation
AuthTestTreeNode::toAuthorizedInvocation() const
{
    SorobanAuthorizedInvocation invocation;
    auto& function = invocation.function.contractFn();
    function.contractAddress = mContractAddress;
    function.functionName = makeSymbol("tree_fn");

    for (auto const& child : mChildren)
    {
        invocation.subInvocations.emplace_back(child.toAuthorizedInvocation());
    }
    return invocation;
}

} // namespace txtext
} // namespace stellar
