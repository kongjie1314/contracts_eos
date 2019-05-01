const Token = artifacts.require("./Token/");
const BancorX = artifacts.require("./BancorX/");
const BancorNetwork = artifacts.require("./BancorNetwork/");
const BancorConverter = artifacts.require("./BancorConverter/");
const UserGeneratedTokens = artifacts.require("./UserGeneratedTokens/");
const UserGeneratedConverters = artifacts.require("./UserGeneratedConverters/");
const XTransferRerouter = artifacts.require("./XTransferRerouter/");

const testAccount1 = { name: 'test1', keys: null };
const testAccount2 = { name: 'test2', keys: null };
const testAccount3 = { name: 'test3', keys: null };

async function regConverter(deployer, token, symbol, fee, networkContract, networkToken, networkTokenSymbol, issuerAccount, issuerPrivateKey) {
    const converter = await deployer.deploy(BancorConverter, `cnvt${token}`);

    const tknContract = await deployer.deploy(Token, token);
    await tknContract.contractInstance.create({
        issuer: tknContract.contract.address,
        maximum_supply: `1000000000.00000000 ${symbol}`},
        { authorization: `${tknContract.contract.address}@active`, broadcast: true, sign: true });

    const tknrlyContract = await deployer.deploy(Token, `tkn${networkTokenSymbol.toLowerCase()}${token}`);
    var rlySymbol = networkTokenSymbol + symbol;
    await tknrlyContract.contractInstance.create({
        issuer: converter.contract.address,
        maximum_supply: `250000000.0000000000 ${rlySymbol}`},
        { authorization: `${tknrlyContract.contract.address}@active`, broadcast: true, sign: true });

    await converter.contractInstance.init({
        smart_contract: tknrlyContract.contract.address,
        smart_currency: `0.0000000000 ${rlySymbol}`,
        smart_enabled: 1,
        enabled: 1,
        network: networkContract.contract.address,
        require_balance: 0,
        max_fee: 30,
        fee
    }, { authorization: `${converter.contract.address}@active`, broadcast: true, sign: true });        

    await converter.contractInstance.setreserve({
        contract:networkToken.contract.address,
        currency: `0.0000000000 ${networkTokenSymbol}`,
        ratio: 500,
        p_enabled: 1
    }, { authorization: `${converter.contract.address}@active`, broadcast: true, sign: true });
        
    await converter.contractInstance.setreserve({
        contract:tknContract.contract.address,
        currency: `0.00000000 ${symbol}`,
        ratio: 500,
        p_enabled: 1
    }, { authorization: `${converter.contract.address}@active`, broadcast: true, sign: true });

    await tknContract.contractInstance.issue({
        to: converter.contract.address,
        quantity: `100000.00000000 ${symbol}`,
        memo: "setup"
    }, { authorization: `${tknContract.contract.address}@active`, broadcast: true, sign: true });
      
    await tknrlyContract.contractInstance.issue({
        to: converter.contract.address,
        quantity: `100000.0000000000 ${networkTokenSymbol + symbol}`,
        memo: "setup"  
    }, { authorization: `${converter.contract.address}@active`, broadcast: true, sign: true, keyProvider: converter.keys.privateKey });
    await networkToken.contractInstance.issue({
        to: converter.contract.address,
        quantity: `100000.0000000000 ${networkTokenSymbol}`,
        memo: "setup"
    }, { authorization: `${issuerAccount}@active`, broadcast: true, sign: true, keyProvider: issuerPrivateKey });
}

module.exports = async function(deployer, network, accounts) {
    const bancorxContract = await deployer.deploy(BancorX, "bancorx");
    const networkContract = await deployer.deploy(BancorNetwork, "thisisbancor");
    const tknbntContract = await deployer.deploy(Token, "bntbntbntbnt");
    await deployer.deploy(UserGeneratedTokens, "bancortokens");
    const userGeneratedConvertersContract = await deployer.deploy(UserGeneratedConverters, "bancorconvrt");
    await userGeneratedConvertersContract.contractInstance.setsettings({
        conversions_enabled: 1,
        max_fee: 30
    }, { authorization: `${userGeneratedConvertersContract.contract.address}@active`});
    await deployer.deploy(XTransferRerouter, "txrerouter");

    const converter = await deployer.deploy(BancorConverter, "bnt2eoscnvrt")
    const bntrlyContract = await deployer.deploy(Token, "bnt2eosrelay");

    const networkTokenSymbol = "BNT";

    // create BNT
    await tknbntContract.contractInstance.create({
        issuer: bancorxContract.contract.address,
        maximum_supply: `250000000.0000000000 ${networkTokenSymbol}`},
        { authorization: `${tknbntContract.contract.address}@active`, broadcast: true, sign: true });


    // create BNTEOS
    await bntrlyContract.contractInstance.create({
        issuer: converter.account,
        maximum_supply: `250000000.0000000000 BNTEOS`
    }, {
        authorization: `${bntrlyContract.account}@active`
    });

    // create BNTEOS converter
    await converter.contractInstance.init({
        smart_contract: bntrlyContract.account,
        smart_currency: `0.0000000000 BNTEOS`,
        smart_enabled: 0,
        enabled: 1,
        network: networkContract.account,
        require_balance: 0,
        max_fee: 0,
        fee: 0
    }, {
        authorization: `${converter.account}@active`
    });

    // set BNT as reserve
    await converter.contractInstance.setreserve({
        contract: tknbntContract.contract.address,
        currency: `0.0000000000 BNT`,
        ratio: 500,
        p_enabled: 1
    }, {
        authorization: `${converter.account}@active`
    });

    // set SYS as reserve
    await converter.contractInstance.setreserve({
        contract: "eosio.token",
        currency: `0.0000 SYS`,
        ratio: 500,
        p_enabled: 1
    }, {
        authorization: `${converter.account}@active`
    });

    // send SYS to converter
    const eosioToken = await bancorxContract.eos.contract('eosio.token')    
    await eosioToken.transfer({
        from: 'eosio',
        to: converter.account,
        quantity: `10000.0000 SYS`,
        memo: 'setup'
    }, {
        authorization: 'eosio@active',
        keyProvider: '5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3'
    })

    // issue BNT to converter
    await tknbntContract.contractInstance.issue({
        to: converter.account,
        quantity: `90000.0000000000 BNT`,
        memo: 'setup'
    }, {
        authorization: `${bancorxContract.account}@active`,
        keyProvider: bancorxContract.keys.privateKey
    });

    // issue BNTEOS
    await bntrlyContract.contractInstance.issue({
        to: converter.account,
        quantity: `20000.0000000000 BNTEOS`,
        memo: 'setup'
    }, {
        authorization: `${converter.account}@active`,
        keyProvider: converter.keys.privateKey
    });


    // initialize bancorx
    await bancorxContract.contractInstance.init({
        x_token_name: tknbntContract.contract.address,
        min_reporters: 2,
        min_limit: 1,
        limit_inc: 100000000000000,
        max_issue_limit: 10000000000000000,
        max_destroy_limit: 10000000000000000},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    await accounts.getCreateAccount('reporter1');
    await accounts.getCreateAccount('reporter2');
    await accounts.getCreateAccount('reporter3');
    await accounts.getCreateAccount('reporter4');
    testAccount1.keys = await accounts.getCreateAccount(testAccount1.name);
    testAccount2.keys = await accounts.getCreateAccount(testAccount2.name);
    testAccount3.keys = await accounts.getCreateAccount(testAccount3.name);

    await bancorxContract.contractInstance.addreporter({
        reporter: 'reporter1'},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    await bancorxContract.contractInstance.addreporter({
        reporter: 'reporter2'},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    await bancorxContract.contractInstance.addreporter({
        reporter: 'reporter3'},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    await bancorxContract.contractInstance.enablext({
        enable: 1},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    await bancorxContract.contractInstance.enablerpt({
        enable: 1},
        {authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    var contract1 = await bancorxContract.eos.contract(tknbntContract.contract.address);
    await contract1.issue({
        to: testAccount1.name,
        quantity: `10000.0000000000 ${networkTokenSymbol}`,
        memo: "test money"
    }, { authorization: `${bancorxContract.contract.address}@active`, broadcast: true, sign: true });
    
    await contract1.issue({
        to: testAccount2.name,
        quantity: `100000.0000000000 ${networkTokenSymbol}`,
        memo: "test money"
    }, { authorization: `${bancorxContract.contract.address}@active`, broadcast: true, sign: true });

    await contract1.issue({
        to: testAccount3.name,
        quantity: `100000.0000000000 ${networkTokenSymbol}`,
        memo: "test money"
    }, { authorization: `${bancorxContract.contract.address}@active`, broadcast: true, sign: true });

    contract1.issue({
        to: 'reporter1',
        quantity: `100.0000000000 ${networkTokenSymbol}`,
        memo: "test money"
        },{authorization: `${bancorxContract.contract.address}@active`,broadcast: true,sign: true});

    for (let i = 0; i < tkns.length; i++) {
        const { contract, symbol, fee } = tkns[i];
        await regConverter(deployer, contract, symbol, fee, networkContract, tknbntContract, networkTokenSymbol, bancorxContract.contract.address, bancorxContract.keys.privateKey);    
    }
    
    for (const { symbol, creator, fee, ratio } of userGeneratedTkns) {
        await tknbntContract.contractInstance.transfer({
            from: creator.name,
            to: userGeneratedConvertersContract.contract.address,
            quantity: `10000.0000000000 ${networkTokenSymbol}`,
            memo: `setup;1000.0000 ${symbol},${fee},${ratio},100000030.0096 ${symbol}`
        }, {
            authorization: `${creator.name}@active`,
            keyProvider: creator.keys.privateKey
        });
    }
        // await registerUserGeneratedConverter(userGeneratedTokensContract, userGeneratedConvertersContract, symbol, creator, networkContract, tknbntContract, networkTokenSymbol, bancorxContract.contract.address, bancorxContract.keys.privateKey);
};

const tkns = [];
tkns.push({ contract: "aa", symbol: "TKNA", fee: 0 });
tkns.push({ contract: "bb", symbol: "TKNB", fee: 1 });
tkns.push({ contract: "cc", symbol: "TKNC", fee: 0 });

const userGeneratedTkns = [];
userGeneratedTkns.push({ symbol: "UGTTKNA", creator: testAccount2, fee: 0, ratio: 200 });
userGeneratedTkns.push({ symbol: "UGTTKNB", creator: testAccount3, fee: 10, ratio: 300 });

