
#pragma once

#include <fstream>
#include <iostream>

#include "clang.h"

#include "wrapped_class.h"

class BidirectionalBindings {
private:
    CompilerInstance & compiler_instance;

    // the "normal" non-bidirectional class being wrapped
    WrappedClass & js_wrapper_class; // the bidirectional type
    WrappedClass & wrapped_class; // the non-birectional type being wrapped

public:
    BidirectionalBindings(CompilerInstance & compiler_instance,
                          WrappedClass & js_wrapper_class,
                          WrappedClass & wrapped_class) :
        compiler_instance(compiler_instance),
        js_wrapper_class(js_wrapper_class),
        wrapped_class(wrapped_class) {}

    std::string short_name(){return wrapped_class.name_alias;}
    std::string canonical_name(){return wrapped_class.class_name;}

    std::vector<const CXXMethodDecl *> get_all_virtual_methods_for_class(const CXXRecordDecl * klass) {
        std::vector<const CXXMethodDecl *> results;
        std::deque<const CXXRecordDecl *> stack{klass};

        while (!stack.empty()) {
            auto current_class = stack.front();
            stack.pop_front();

            for(CXXMethodDecl * method : current_class->methods()) {
                if (dyn_cast<CXXDestructorDecl>(method)) {
                    //if (print_logging) cerr << "Skipping virtual destructor while gathering virtual methods" << endl;
                    continue;
                }
                if (dyn_cast<CXXConversionDecl>(method)) {
                    //if (print_logging) cerr << "Skipping user-defined conversion operator" << endl;
                    continue;
                }
                if (method->isVirtual() && !method->isPure()) {
                    // go through existing ones and check for match
                    if (std::find_if(results.begin(), results.end(), [&](auto found){
                        if(get_method_string(compiler_instance, wrapped_class, method) ==
                           get_method_string(compiler_instance, wrapped_class, found)) {
//                                printf("Found dupe: %s\n", get_method_string(method).c_str());
                            return true;
                        }
                        return false;
                    }) == results.end()) {
                        results.push_back(method);
                    }
                }
            }

            for (auto base_class : current_class->bases()) {
                auto base_decl = base_class.getType()->getAsCXXRecordDecl();
                stack.push_back(base_decl);
            }
        }
        return results;
    }

#if 0
    std::string handle_class(const CXXRecordDecl * klass) {
        std::stringstream result;
        auto virtuals = get_all_virtual_methods_for_class(klass);
        for (auto method : virtuals) {
            result << handle_virtual(method);
        }
        return result.str();

    }

    void generate_bindings();
#endif
};

