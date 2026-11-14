// Smart pointers: unique_ptr, shared_ptr (shared objects and cycles), and derived classes.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <jsonser/jsonser.hpp>

namespace model {

// A base class with derived classes. It must have a virtual function (here the destructor).
struct Shape {
    std::string label;
    virtual ~Shape() = default;
    virtual double area() const { return 0; }

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(label);
    }
};

struct Circle : Shape {
    double r = 0;
    double area() const override { return 3.14159265 * r * r; }

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Shape>(*this) & JSONSER_NVP(r);  // base fields + own fields
    }
};

struct Rect : Shape {
    double w = 0, h = 0;
    double area() const override { return w * h; }

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Shape>(*this) & JSONSER_NVP(w) & JSONSER_NVP(h);
    }
};

// A linked node: `next` may point back to an earlier node (a cycle).
struct Node {
    std::string name;
    std::shared_ptr<Node> next;

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(name) & JSONSER_NVP(next);
    }
};

struct Scene {
    std::unique_ptr<Shape> background;           // one owner: written as the object itself (+ "$type")
    std::vector<std::shared_ptr<Shape>> shapes;  // shared: the first time {"$id":n,...}, later {"$ref":n}
    std::shared_ptr<Shape> selected;             // points to one of `shapes` -> saved as {"$ref":n}

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(background) & JSONSER_NVP(shapes) & JSONSER_NVP(selected);
    }
};

}  // namespace model

// Every derived class you save through a base pointer must be registered. The name ("Circle")
// is what appears in the file as "$type", so loading can create the right class.
// _NAMED gives a short name; plain JSONSER_EXPORT(T, Base) uses the text "model::Circle".
JSONSER_EXPORT_NAMED(model::Circle, model::Shape, "Circle")
JSONSER_EXPORT_NAMED(model::Rect, model::Shape, "Rect")
