#include <math.h>
#include <algorithm>
#include "./UserGeneratedConverters.hpp"
#include "../UserGeneratedTokens/UserGeneratedTokens.hpp"
#include "../Common/common.hpp"

using namespace eosio;

ACTION UserGeneratedConverters::create(name owner,
                             asset currency,
                             bool  smart_enabled,
                             bool  require_balance,
                             uint16_t fee) {
    require_auth(_self);
    _create(owner, currency, smart_enabled, require_balance, fee);
}

void UserGeneratedConverters::_create(name owner,
                             asset currency,
                             bool  smart_enabled,
                             bool  require_balance,
                             uint16_t fee) {
    settings settings_table(_self, _self.value);
    const auto& st = settings_table.get();
    eosio_assert(fee <= st.max_fee, "fee must be lower or equal to the maximum fee");

    converters converters_table(_self, currency.symbol.code().raw());
    const auto& converter = converters_table.find(currency.symbol.code().raw());
    eosio_assert(converter == converters_table.end(), "converter for the given currency code already exists");
    
    converters_table.emplace(_self, [&](auto& c) {
        c.currency        = currency;
        c.owner           = owner;
        c.smart_enabled   = smart_enabled;
        c.require_balance = require_balance;
        c.fee             = fee;
        c.reserves        = {};
        c.enabled         = true;
    });
}

void UserGeneratedConverters::createatomic(name owner,
                             asset quantity,
                             string memo,
                             name code) {
    const auto& parsed_memo = parse_converter_creation_memo(memo);

    eosio_assert(code == BNT_TOKEN && quantity.symbol == BNT_SYMBOL, "unknown reserve currency");
    eosio_assert(parsed_memo.maximum_supply.symbol == parsed_memo.initial_supply.symbol, "symbol mismatch");
    
    _create(owner, parsed_memo.maximum_supply, true, false, parsed_memo.fee);
    
    _setreserve(parsed_memo.maximum_supply.symbol.code(), BNT_TOKEN, asset(0, BNT_SYMBOL), parsed_memo.ratio, true, parsed_memo.initial_supply.amount);
    add_reserve_balance(parsed_memo.maximum_supply.symbol.code(), quantity);

    action(
            permission_level{ _self, "active"_n },
            USER_GENERATED_TOKENS, "create"_n,
            std::make_tuple(parsed_memo.maximum_supply) 
        ).send();
    
    action(
            permission_level{ _self, "active"_n },
            USER_GENERATED_TOKENS, "issue"_n,
            std::make_tuple(owner, parsed_memo.initial_supply , string("setup")) 
        ).send();
}

ACTION UserGeneratedConverters::setsettings(bool enabled, uint16_t max_fee) {
    require_auth(_self);

    eosio_assert(max_fee <= 1000, "max fee must be lower or equal to 1000");

    settings settings_table(_self, _self.value);

    settings_t new_settings;
    new_settings.enabled  = enabled;
    new_settings.max_fee              = max_fee;

    settings_table.set(new_settings, _self);
}

ACTION UserGeneratedConverters::update(asset currency, bool smart_enabled, bool enabled, bool require_balance, uint16_t fee) {
    require_auth(_self);

    settings settings_table(_self, _self.value);
    const auto& st = settings_table.get();
    eosio_assert(fee <= st.max_fee, "fee must be lower or equal to the maximum fee");

    converters converters_table(_self, currency.symbol.code().raw());
    auto converter = converters_table.require_find(currency.symbol.code().raw(), "converter does not exist");

    converters_table.modify(converter, _self, [&](auto& c) {
            c.smart_enabled   = smart_enabled;
            c.require_balance = require_balance;
            c.fee             = fee;
            c.enabled         = enabled;
        });
}

ACTION UserGeneratedConverters::setreserve(symbol_code converter_currency_code, name contract, asset currency, uint64_t ratio, bool p_enabled) {
    require_auth(_self);

    converters converters_table(_self, converter_currency_code.raw());
    auto converter = converters_table.require_find(converter_currency_code.raw(), "converter does not exist");
    auto current_smart_supply = (get_supply(USER_GENERATED_TOKENS, converter->currency.symbol.code())).amount + converter->currency.amount;
    
    _setreserve(converter_currency_code, contract, currency, ratio, p_enabled, current_smart_supply);
}

void UserGeneratedConverters::add_reserve_balance(symbol_code converter_currency_code, asset value) {
    converters converters_table(_self, converter_currency_code.raw());
    auto converter = converters_table.require_find(converter_currency_code.raw(), "converter does not exist");
    
    auto reserve = std::find_if(converter->reserves.begin(), converter->reserves.end(), [&reserve_sym = value.symbol](const auto& r) -> bool {
        return r.currency.symbol.code().raw() == reserve_sym.code().raw();
        });
    
    eosio_assert(reserve != converter->reserves.end(), "reserve not found");

    converters_table.modify(converter, _self, [&](auto& c) {
        int reserve_index = std::distance(converter->reserves.begin(), reserve);
        c.reserves[reserve_index].balance += value;
    });
}

void UserGeneratedConverters::sub_reserve_balance(symbol_code converter_currency_code, asset value) {
    converters converters_table(_self, converter_currency_code.raw());
    auto converter = converters_table.require_find(converter_currency_code.raw(), "converter does not exist");
    
    auto reserve = std::find_if(converter->reserves.begin(), converter->reserves.end(), [&reserve_sym = value.symbol](const auto& r) -> bool {
        return r.currency.symbol.code().raw() == reserve_sym.code().raw();
        });
    
    eosio_assert(reserve != converter->reserves.end(), "reserve not found");

    converters_table.modify(converter, _self, [&](auto& c) {
        int reserve_index = std::distance(converter->reserves.begin(), reserve);
        c.reserves[reserve_index].balance -= value;
    });
}

void UserGeneratedConverters::_setreserve(symbol_code converter_currency_code, name contract, asset currency, uint64_t ratio, bool p_enabled, uint64_t smart_token_supply) {
    eosio_assert(ratio > 0 && ratio <= 1000, "ratio must be between 1 and 1000");
    
    converters converters_table(_self, converter_currency_code.raw());
    auto converter = converters_table.require_find(converter_currency_code.raw(), "converter does not exist");
    
    auto reserve = std::find_if(converter->reserves.begin(), converter->reserves.end(), [&c = currency](const auto& r) -> bool {
        return r.currency.symbol.code().raw() == c.symbol.code().raw();
        });
    

    converters_table.modify(converter, _self, [&](auto& c) {
        if (reserve == c.reserves.end()) {
            c.reserves.push_back({ contract, currency, ratio, p_enabled, asset(0, currency.symbol) });
        }
        else {
            eosio_assert(reserve->contract == contract, "cannot update the reserve contract name");
            int reserve_index = std::distance(converter->reserves.begin(), reserve);
            
            c.reserves[reserve_index].ratio = ratio;
            c.reserves[reserve_index].p_enabled = p_enabled;
        }
    });

    uint64_t total_ratio = 0;
    for (auto& reserve : converter->reserves)
        total_ratio += reserve.ratio;
    
    eosio_assert(total_ratio <= 1000, "total ratio cannot exceed 1000");
    
    if (reserve != converter->reserves.end()) {
        auto reserve_balance = (reserve->balance.amount + currency.amount) / pow(10, currency.symbol.precision()); 
        EMIT_PRICE_DATA_EVENT(smart_token_supply / pow(10, converter->currency.symbol.precision()), contract, currency.symbol.code(), reserve_balance, ratio);
    }
}

void UserGeneratedConverters::convert(name from, asset quantity, string memo, name code) {
    eosio_assert(quantity.is_valid(), "invalid quantity");
    eosio_assert(quantity.amount != 0, "zero quantity is disallowed");
    auto from_amount = quantity.amount / pow(10, quantity.symbol.precision());

    auto memo_object = parse_memo(memo);
    eosio_assert(memo_object.path.size() > 1, "invalid memo format");
    
    settings settings_table(_self, _self.value);
    auto settings = settings_table.get();

    converters converters_table(_self, symbol_code(memo_object.converters[0].sym).raw());
    auto converter = converters_table.require_find(symbol_code(memo_object.converters[0].sym).raw(), "converter does not exist");

    eosio_assert(settings.enabled && converter->enabled, "conversions are disabled");
    eosio_assert(from == BANCOR_NETWORK, "converter can only receive from network contract");

    eosio_assert(memo_object.converters[0].account == _self, "wrong converter");
    auto from_path_currency = quantity.symbol.code().raw();
    auto to_path_currency = symbol_code(memo_object.path[1].c_str()).raw();
    eosio_assert(from_path_currency != to_path_currency, "cannot convert to self");

    auto smart_symbol_name = converter->currency.symbol.code().raw();
    auto from_token = get_reserve(from_path_currency, *converter);
    auto to_token = get_reserve(to_path_currency, *converter);

    auto from_currency = from_token.currency;
    auto to_currency = to_token.currency;

    auto to_currency_precision = to_currency.symbol.precision();
    
    auto from_contract = from_token.contract;
    auto to_contract = to_token.contract;

    bool incoming_smart_token = (from_currency.symbol.code().raw() == smart_symbol_name);
    bool outgoing_smart_token = (to_currency.symbol.code().raw() == smart_symbol_name);
    
    auto from_ratio = from_token.ratio;
    auto to_ratio = to_token.ratio;

    eosio_assert(to_token.p_enabled, "'to' token purchases disabled");
    eosio_assert(code == from_contract, "unknown 'from' contract");
    
    double current_from_balance;
    double current_to_balance;
    if (incoming_smart_token) {
        current_from_balance = ((get_balance(from_contract, _self, from_currency.symbol.code())).amount + from_currency.amount - quantity.amount) / pow(10, from_currency.symbol.precision());
        current_to_balance = (to_token.balance.amount + to_currency.amount) / pow(10, to_currency_precision);
    }
    else if (outgoing_smart_token) {
        current_from_balance = (from_token.balance.amount + from_currency.amount - quantity.amount) / pow(10, from_currency.symbol.precision());
        current_to_balance = ((get_balance(to_contract, _self, to_currency.symbol.code())).amount + to_currency.amount) / pow(10, to_currency_precision);

        add_reserve_balance(converter->currency.symbol.code(), quantity);
    }
    else {
        current_from_balance = (from_token.balance.amount + from_currency.amount - quantity.amount) / pow(10, from_currency.symbol.precision());
        current_to_balance = ((get_balance(to_contract, _self, to_currency.symbol.code())).amount + to_currency.amount) / pow(10, to_currency_precision);

        add_reserve_balance(converter->currency.symbol.code(), quantity);
    }

    auto current_smart_supply = ((get_supply(USER_GENERATED_TOKENS, converter->currency.symbol.code())).amount + converter->currency.amount) / pow(10, converter->currency.symbol.precision());

    name final_to = name(memo_object.dest_account.c_str());
    double smart_tokens = 0;
    double to_tokens = 0;
    double total_fee_amount = 0;
    bool quick = false;
    if (incoming_smart_token) {
        // destory received token
        action(
            permission_level{ _self, "active"_n },
            USER_GENERATED_TOKENS, "retire"_n,
            std::make_tuple(quantity, std::string("destroy on conversion"))
        ).send();

        smart_tokens = from_amount;
        current_smart_supply -= smart_tokens;
    }
    else if (!incoming_smart_token && !outgoing_smart_token && (from_ratio == to_ratio) && (converter->fee == 0)) {
        to_tokens = quick_convert(current_from_balance, from_amount, current_to_balance);
        quick = true;
    }
    else {
        smart_tokens = calculate_purchase_return(current_from_balance, from_amount, current_smart_supply, from_ratio);
        current_smart_supply += smart_tokens;
        if (converter->fee > 0) {
            double ffee = (1.0 * converter->fee / 1000.0);
            auto fee = smart_tokens * ffee;
            if (fee > 0) {
                smart_tokens = smart_tokens - fee;
                total_fee_amount += fee;
            }
        }
    }

    auto issue = false;
    if (outgoing_smart_token) {
        eosio_assert(memo_object.path.size() == 2, "smart token must be final currency");
        to_tokens = smart_tokens;
        issue = true;
    }
    else if (!quick) {
        if (converter->fee) {
            double ffee = (1.0 * converter->fee / 1000.0);
            auto fee = smart_tokens * ffee;
            if (fee > 0) {
                smart_tokens = smart_tokens - fee;
                total_fee_amount += fee;
            }
        }

        to_tokens = calculate_sale_return(current_to_balance, smart_tokens, current_smart_supply, to_ratio);
    }

    int64_t to_amount = (to_tokens * pow(10, to_currency_precision));

    double formatted_total_fee_amount = (int)(total_fee_amount * pow(10, to_currency_precision)) / pow(10, to_currency_precision);
    EMIT_CONVERSION_EVENT(memo, from_token.contract, from_currency.symbol.code(), to_token.contract, to_currency.symbol.code(), from_amount, (to_amount / pow(10, to_currency_precision)), formatted_total_fee_amount);

    if (incoming_smart_token || !outgoing_smart_token)
        EMIT_PRICE_DATA_EVENT(current_smart_supply, to_token.contract, to_currency.symbol.code(), current_to_balance - to_amount, (to_ratio / 1000.0));
    if (outgoing_smart_token || !incoming_smart_token)
        EMIT_PRICE_DATA_EVENT(current_smart_supply, from_token.contract, from_currency.symbol.code(), current_from_balance, (from_ratio / 1000.0));

    path new_path = memo_object.path;
    new_path.erase(new_path.begin(), new_path.begin() + 2);
    memo_object.path = new_path;

    auto new_memo = build_memo(memo_object);
    auto new_asset = asset(to_amount, to_currency.symbol);
    name inner_to = BANCOR_NETWORK;
    if (memo_object.path.size() == 0) {
        inner_to = final_to;
        verify_min_return(new_asset, memo_object.min_return);
        if (converter->require_balance)
            verify_entry(inner_to, to_contract, new_asset);
        new_memo = memo_object.receiver_memo;
    }

    if (issue)
        action(
            permission_level{ _self, "active"_n },
            to_contract, "issue"_n,
            std::make_tuple(inner_to, new_asset, new_memo) 
        ).send();
    else {
        sub_reserve_balance(converter->currency.symbol.code(), new_asset);
        
        action(
            permission_level{ _self, "active"_n },
            to_contract, "transfer"_n,
            std::make_tuple(_self, inner_to, new_asset, new_memo)
        ).send();
    }
}

 // returns a reserve object
 // can also be called for the smart token itself
const UserGeneratedConverters::Reserve& UserGeneratedConverters::get_reserve(uint64_t name, const converter_t& converter) {
    if (converter.currency.symbol.code().raw() == name) {
        static UserGeneratedConverters::Reserve temp_reserve = { USER_GENERATED_TOKENS, converter.currency, 0, converter.smart_enabled };
        return temp_reserve;
    }

    const auto& reserve = std::find_if(converter.reserves.begin(), converter.reserves.end(), [&n = name](const auto& r) -> bool {
        return r.currency.symbol.code().raw() == n;
        });

    eosio_assert(reserve != converter.reserves.end(), "reserve not found");

    return *reserve;
}

// returns the balance object for an account
asset UserGeneratedConverters::get_balance(name contract, name owner, symbol_code sym) {
    UserGeneratedTokens::accounts accountstable(contract, owner.value);
    const auto& ac = accountstable.get(sym.raw());
    return ac.balance;
}

// returns a token supply
asset UserGeneratedConverters::get_supply(name contract, symbol_code sym) {
    UserGeneratedTokens::stats statstable(contract, sym.raw());
    const auto& st = statstable.get(sym.raw());
    return st.supply;
}

// asserts if the supplied account doesn't have an entry for a given token
void UserGeneratedConverters::verify_entry(name account, name currency_contact, eosio::asset currency) {
    UserGeneratedTokens::accounts accountstable(currency_contact, account.value);
    auto ac = accountstable.find(currency.symbol.code().raw());
    eosio_assert(ac != accountstable.end(), "must have entry for token (claim token first)");
}

// asserts if a conversion resulted in an amount lower than the minimum amount defined by the caller
void UserGeneratedConverters::verify_min_return(eosio::asset quantity, std::string min_return) {
	float ret = stof(min_return.c_str());
    int64_t ret_amount = (ret * pow(10, quantity.symbol.precision()));
    eosio_assert(quantity.amount >= ret_amount, "below min return");
}

// given a token supply, reserve balance, ratio and a input amount (in the reserve token),
// calculates the return for a given conversion (in the main token)
double UserGeneratedConverters::calculate_purchase_return(double balance, double deposit_amount, double supply, int64_t ratio) {
    double R(supply);
    double C(balance + deposit_amount);
    double F(ratio / 1000.0);
    double T(deposit_amount);
    double ONE(1.0);

    double E = -R * (ONE - pow(ONE + T / C, F));
    return E;
}

// given a token supply, reserve balance, ratio and a input amount (in the main token),
// calculates the return for a given conversion (in the reserve token)
double UserGeneratedConverters::calculate_sale_return(double balance, double sell_amount, double supply, int64_t ratio) {
    double R(supply - sell_amount);
    double C(balance);
    double F(1000.0 / ratio);
    double E(sell_amount);
    double ONE(1.0);

    double T = C * (pow(ONE + E/R, F) - ONE);
    return T;
}

double UserGeneratedConverters::quick_convert(double balance, double in, double toBalance) {
    return in / (balance + in) * toBalance;
}

float UserGeneratedConverters::stof(const char* s) {
    float rez = 0, fact = 1;
    if (*s == '-') {
        s++;
        fact = -1;
    }

    for (int point_seen = 0; *s; s++) {
        if (*s == '.') {
            point_seen = 1; 
            continue;
        }

        int d = *s - '0';
        if (d >= 0 && d <= 9) {
            if (point_seen) fact /= 10.0f;
            rez = rez * 10.0f + (float)d;
        }
    }

    return rez * fact;
};

void UserGeneratedConverters::transfer(name from, name to, asset quantity, string memo) {
    if (from == _self) {
        return;
    }

    if (to != _self) 
        return;
    
    const auto& splitted_memo = split(memo, ";");
    if (splitted_memo[0] == "setup")
        createatomic(from, quantity, splitted_memo[1], _code);
    else
        convert(from, quantity, memo, _code); 
}

extern "C" {
    [[noreturn]] void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        if (action == "transfer"_n.value && code != receiver) {
            eosio::execute_action(eosio::name(receiver), eosio::name(code), &UserGeneratedConverters::transfer);
        }
        if (code == receiver) {
            switch (action) {
                EOSIO_DISPATCH_HELPER(UserGeneratedConverters, (create)(setsettings)(update)(setreserve)) 
            }    
        }
        eosio_exit(0);
    }
}
