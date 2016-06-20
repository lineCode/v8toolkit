#include <assert.h>
#include <sstream>
#include <set>

#include "v8helpers.h"

namespace v8toolkit {

int get_array_length(v8::Isolate * isolate, v8::Local<v8::Array> array) {
    auto context = isolate->GetCurrentContext();
    return array->Get(context, v8::String::NewFromUtf8(isolate, "length")).ToLocalChecked()->Uint32Value(); 
}


int get_array_length(v8::Isolate * isolate, v8::Local<v8::Value> array_value)
{
    if(array_value->IsArray()) {
        return get_array_length(isolate, v8::Local<v8::Array>::Cast(array_value));
    } else {
        // TODO: probably throw?
        assert(array_value->IsArray());
    }
    assert(false); // shut up the compiler
}


void set_global_object_alias(v8::Isolate * isolate, const v8::Local<v8::Context> context, std::string alias_name)
{
    auto global_object = context->Global();
    (void)global_object->Set(context, v8::String::NewFromUtf8(isolate, alias_name.c_str()), global_object);
    
}



void print_v8_value_details(v8::Local<v8::Value> local_value) {
    
    auto value = *local_value;
    
    std::cout << "undefined: " << value->IsUndefined() << std::endl;
    std::cout << "null: " << value->IsNull() << std::endl;
    std::cout << "true: " << value->IsTrue() << std::endl;
    std::cout << "false: " << value->IsFalse() << std::endl;
    std::cout << "name: " << value->IsName() << std::endl;
    std::cout << "string: " << value->IsString() << std::endl;
    std::cout << "symbol: " << value->IsSymbol() << std::endl;
    std::cout << "function: " << value->IsFunction() << std::endl;
    std::cout << "array: " << value->IsArray() << std::endl;
    std::cout << "object: " << value->IsObject() << std::endl;
    std::cout << "boolean: " << value->IsBoolean() << std::endl;
    std::cout << "number: " << value->IsNumber() << std::endl;
    std::cout << "external: " << value->IsExternal() << std::endl;
    std::cout << "isint32: " << value->IsInt32() << std::endl;
    std::cout << "isuint32: " << value->IsUint32() << std::endl;
    std::cout << "date: " << value->IsDate() << std::endl;
    std::cout << "argument object: " << value->IsArgumentsObject() << std::endl;
    std::cout << "boolean object: " << value->IsBooleanObject() << std::endl;
    std::cout << "number object: " << value->IsNumberObject() << std::endl;
    std::cout << "string object: " << value->IsStringObject() << std::endl;
    std::cout << "symbol object: " << value->IsSymbolObject() << std::endl;
    std::cout << "native error: " << value->IsNativeError() << std::endl;
    std::cout << "regexp: " << value->IsRegExp() << std::endl;
    std::cout << "generator function: " << value->IsGeneratorFunction() << std::endl;
    std::cout << "generator object: " << value->IsGeneratorObject() << std::endl;
}

std::set<std::string> make_set_from_object_keys(v8::Isolate * isolate,
                                                v8::Local<v8::Object> & object,
                                                bool own_properties_only)
{
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Array> properties;
    if (own_properties_only) {
        properties = object->GetOwnPropertyNames(context).ToLocalChecked();
    } else {
        properties = object->GetPropertyNames(context).ToLocalChecked();
    }
    auto array_length = get_array_length(isolate, properties);

    std::set<std::string> keys;

    for (int i = 0; i < array_length; i++) {
        keys.insert(*v8::String::Utf8Value(properties->Get(context, i).ToLocalChecked()));
    }

    return keys;
}




#define STRINGIFY_VALUE_DEBUG false

std::string stringify_value(v8::Isolate * isolate, const v8::Local<v8::Value> & value, bool top_level, bool show_all_properties)
{
    static std::vector<v8::Local<v8::Value>> processed_values;


    if (top_level) {
        processed_values.clear();
    };

    auto context = isolate->GetCurrentContext();

    std::stringstream output;

    // Only protect against cycles on container types - otherwise a numeric value with
    //   the same number won't get shown twice
    if (value->IsObject() || value->IsArray()) {
        for(auto processed_value : processed_values) {
            if(processed_value == value) {
                if (STRINGIFY_VALUE_DEBUG) print_v8_value_details(value);
                if (STRINGIFY_VALUE_DEBUG) printf("Skipping previously processed value\n");
                return "";
            }
        }
        processed_values.push_back(value);
    }


    if(value.IsEmpty()) {
        if (STRINGIFY_VALUE_DEBUG) printf("Value IsEmpty\n");
        return "Value specified as an empty v8::Local";
    }

    // if the left is a bool, return true if right is a bool and they match, otherwise false
    if (value->IsBoolean() || value->IsNumber() || value->IsFunction() || value->IsUndefined() || value->IsNull()) {
        if (STRINGIFY_VALUE_DEBUG) printf("Stringify: treating value as 'normal'\n");
        output << *v8::String::Utf8Value(value);
    } else if (value->IsString()) {
        output << "\"" << *v8::String::Utf8Value(value) << "\"";
    } else if (value->IsArray()) {
        // printf("Stringify: treating value as array\n");
        auto array = v8::Local<v8::Array>::Cast(value);
        auto array_length = get_array_length(isolate, array);

        output << "[";
        auto first_element = true;
        for (int i = 0; i < array_length; i++) {
            if (!first_element) {
                output << ", ";
            }
            first_element = false;
            auto value = array->Get(context, i);
            output << stringify_value(isolate, value.ToLocalChecked(), false, show_all_properties);
        }
        output << "]";
    } else {
        // printf("Stringify: treating value as object\n");
        // check this last in case it's some other type of more specialized object we will test the specialization instead (like an array)
        // objects must have all the same keys and each key must have the same as determined by calling this function on each value
        // printf("About to see if we can stringify this as an object\n");
        // print_v8_value_details(value);
        auto object = v8::Local<v8::Object>::Cast(value);
        if(value->IsObject() && !object.IsEmpty()) {
            if (STRINGIFY_VALUE_DEBUG) printf("Stringifying object\n");
            output << "{";
            auto keys = make_set_from_object_keys(isolate, object, !show_all_properties);
            auto first_key = true;
            for(auto key : keys) {
                if (STRINGIFY_VALUE_DEBUG) printf("Stringify: object key %s\n", key.c_str());
                if (!first_key) {
                    output << ", ";
                }
                first_key = false;
                output << key;
                output << ": ";
                auto value = object->Get(context, v8::String::NewFromUtf8(isolate, key.c_str()));
                output << stringify_value(isolate, value.ToLocalChecked(), false, show_all_properties);
            }
            output << "}";
        }
    }
    return output.str();
}


v8::Local<v8::Value> get_key(v8::Local<v8::Context> context, v8::Local<v8::Object> object, std::string key) {
    return get_key_as<v8::Value>(context, object, key);
}

v8::Local<v8::Value> get_key(v8::Local<v8::Context> context, v8::Local<v8::Value> value, std::string key) {
    return get_key_as<v8::Value>(context, get_value_as<v8::Object>(value), key);
}



}
