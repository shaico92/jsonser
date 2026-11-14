// custom_types: control the JSON a type produces. The types are in model/custom.hpp:
//   thirdparty::Timestamp  free json_serialize()    -> {"year":2026,"month":11,"day":14}
//   thirdparty::Money      free json_save()/json_load() -> {"amount":"12.34","currency":"EUR"}
//   model::Vec3            serializer<> specialization -> [1,2.5,-3]
//   model::Settings        private members, json_save()/json_load(), enum as text -> "difficulty":"hard"
#include <iostream>
#include <sstream>

#include <jsonser/jsonser.hpp>

#include "model/custom.hpp"

// Everything one save holds. Several names can go into one archive with << ... << ...
struct Save {
    thirdparty::Timestamp created{2026, 11, 14};
    thirdparty::Money balance{1234, "EUR"};
    model::Settings settings{"Lowlands", model::Difficulty::hard, {1, 2.5, -3}};
};

int main() {
    try {
        Save in;
        std::ostringstream os;
        {
            jsonser::json_oarchive oa(os, {2});  // pretty-printed
            oa << jsonser::make_nvp("created", in.created)  //
               << jsonser::make_nvp("balance", in.balance)  //
               << jsonser::make_nvp("settings", in.settings);
        }
        std::cout << os.str() << "\n";

        Save out;  // load into fresh objects
        out.balance.cents = 0;
        std::istringstream is(os.str());
        jsonser::json_iarchive ia(is);
        ia >> jsonser::make_nvp("created", out.created)  //
           >> jsonser::make_nvp("balance", out.balance)  //
           >> jsonser::make_nvp("settings", out.settings);

        std::cout << "loaded: " << out.created.year << "-" << out.created.month << "-" << out.created.day
                  << ", balance " << out.balance.cents << " cents, world " << out.settings.world()
                  << ", hard: " << (out.settings.difficulty() == model::Difficulty::hard ? "yes" : "no")
                  << ", spawn y " << out.settings.spawn().y << "\n";
    } catch (const jsonser::archive_error& e) {
        std::cerr << "failed: " << e.what() << "\n";
        return 1;
    }
}
