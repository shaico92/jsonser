// pointers: unique_ptr, shared_ptr (sharing and cycles) and derived classes through a base pointer.
// The classes are in model/scene.hpp. (Raw pointers are shown in basic.cpp / model/inventory.hpp.)
#include <iostream>
#include <sstream>

#include <jsonser/jsonser.hpp>

#include "model/scene.hpp"

using namespace model;

// Save to a string and load back - the same as a file, just easier to print.
template <class T>
static std::string save(const char* name, const T& value) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
        oa << jsonser::make_nvp(name, value);
    }
    return os.str();
}
template <class T>
static void load(const std::string& json, const char* name, T& value) {
    std::istringstream is(json);
    jsonser::json_iarchive ia(is);
    ia >> jsonser::make_nvp(name, value);
}

// 1) Derived classes + shared objects.
static void scene_example() {
    Scene scene;
    auto bg = std::make_unique<Rect>();
    bg->label = "background";
    bg->w = 100;
    bg->h = 50;
    scene.background = std::move(bg);  // unique_ptr<Shape> holding a Rect

    auto sun = std::make_shared<Circle>();
    sun->label = "sun";
    sun->r = 5;
    auto box = std::make_shared<Rect>();
    box->label = "box";
    box->w = box->h = 2;
    scene.shapes = {sun, box};
    scene.selected = sun;  // the same object as shapes[0]

    std::string json = save("scene", scene);
    std::cout << "scene JSON:\n  " << json << "\n";
    // Look for: "$type":"Circle" (the real class), "$id":1 on the sun, and "selected":{"$ref":1}.

    Scene back;
    load(json, "scene", back);
    std::cout << "loaded: background area " << back.background->area() << ", "
              << back.shapes.size() << " shapes, selected is shapes[0]: "
              << (back.selected == back.shapes[0] ? "yes (still shared)" : "no") << "\n\n";
}

// 2) A cycle: a -> b -> a. Each object is written once; the back-link is {"$ref":1}.
static void cycle_example() {
    auto a = std::make_shared<Node>();
    auto b = std::make_shared<Node>();
    a->name = "a";
    b->name = "b";
    a->next = b;
    b->next = a;

    std::string json = save("list", a);
    std::cout << "cycle JSON:\n  " << json << "\n";

    std::shared_ptr<Node> back;
    load(json, "list", back);
    std::cout << "loaded: " << back->name << " -> " << back->next->name << " -> " << back->next->next->name
              << " (back at the start: " << (back->next->next == back ? "yes" : "no") << ")\n";

    a->next.reset();  // break the cycles so the objects are freed
    back->next->next.reset();
}

int main() {
    try {
        scene_example();
        cycle_example();
    } catch (const jsonser::archive_error& e) {
        std::cerr << "failed: " << e.what() << "\n";
        return 1;
    }
}
