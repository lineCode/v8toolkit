#pragma once

// Tell EASTL not to redefine initializer_list
#define EA_HAVE_CPP11_INITIALIZER_LIST 1

#include <EASTL/vector_map.h>
#include <EASTL/vector_multimap.h>
#include <EASTL/utility.h>
#include <EASTL/string.h>
#include <EASTL/fixed_string.h>
#include <EASTL/vector_set.h>
#include <EASTL/vector.h>

namespace v8toolkit {



template<class FirstT, class SecondT>
struct v8toolkit::CastToNative<eastl::pair<FirstT, SecondT>>{
    eastl::pair<FirstT, SecondT> operator()(v8::Isolate * isolate, v8::Local<v8::Value> value) const {
        pair_type_helper<eastl::pair, FirstT, SecondT>(isolate, value);
    }
};


// EASTL VECTOR_MAP
template<class Key, class Value, class... Args>
struct CastToNative<eastl::vector_map<Key, Value, Args...>> {
    eastl::vector_map<Key, Value, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return map_type_helper<eastl::vector_map, Key, Value, Args...>(isolate, value);
    }
};
template<class Key, class Value, class... Args>
struct CastToNative<eastl::vector_map<Key, Value, Args...> const> {
    eastl::vector_map<Key, Value, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return map_type_helper<eastl::vector_map, Key, Value, Args...>(isolate, value);
    }
};


// EASTL VECTOR
template<class T, class... Args>
struct CastToNative<eastl::vector<T, Args...>> {
    eastl::vector<T, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return vector_type_helper<eastl::vector, T, Args...>(isolate, value);
    }
};
template<class T, class... Args>
struct CastToNative<eastl::vector<T, Args...> const> {
    using ResultType = eastl::vector<std::remove_reference_t<std::result_of_t<CastToNative<T>(v8::Isolate *, v8::Local<v8::Value>)>>, Args...>;

    ResultType operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return vector_type_helper<eastl::vector, T, Args...>(isolate, value);
    }
};


// EASTL VECTOR_SET
template<class T, class... Args>
struct CastToNative<eastl::vector_set<T, Args...>> {
    eastl::vector_set<T, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        // sue me.
        auto vector = vector_type_helper<eastl::vector, T>(isolate, value);
        eastl::vector_set<T, Args...> set;
        for(auto & i : vector) {
            set.emplace(std::move(i));
        }
        return set;
    }
};
// EASTL VECTOR_SET
template<class T, class... Args>
struct CastToNative<eastl::vector_set<T, Args...> const > {
    eastl::vector_set<T, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        // sue me.
        auto vector = vector_type_helper<eastl::vector, T>(isolate, value);
        eastl::vector_set<T, Args...> set;
        for(auto & i : vector) {
            set.emplace(std::move(i));
        }
        return set;
    }
};






template<class Key, class Value, class... Args>
struct CastToNative<eastl::vector_multimap<Key, Value, Args...>> {
    eastl::vector_multimap<Key, Value, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return multimap_type_helper<eastl::vector_multimap, Key, Value, Args...>(isolate, value);
    }
};
template<class Key, class Value, class... Args>
struct CastToNative<eastl::vector_multimap<Key, Value, Args...> const> {
    eastl::vector_multimap<Key, Value, Args...> operator()(v8::Isolate *isolate, v8::Local <v8::Value> value) const {
        return multimap_type_helper<eastl::vector_multimap, Key, Value, Args...>(isolate, value);
    }
};

CAST_TO_NATIVE_PRIMITIVE_WITH_CONST(eastl::string)
    return eastl::string(*v8::String::Utf8Value(value));
}

CAST_TO_JS(eastl::string){return v8::String::NewFromUtf8(isolate, value.c_str());}


CAST_TO_NATIVE_WITH_CONST(eastl::fixed_string<CharType V8TOOLKIT_COMMA Length V8TOOLKIT_COMMA Overflow V8TOOLKIT_COMMA Allocator>, class CharType V8TOOLKIT_COMMA int Length V8TOOLKIT_COMMA bool Overflow V8TOOLKIT_COMMA class Allocator)
    return eastl::fixed_string<CharType, Length, Overflow, Allocator>(*v8::String::Utf8Value(value));
    }
};

CAST_TO_JS_TEMPLATED(eastl::fixed_string<CharType V8TOOLKIT_COMMA Length V8TOOLKIT_COMMA Overflow V8TOOLKIT_COMMA Allocator>, class CharType V8TOOLKIT_COMMA int Length V8TOOLKIT_COMMA bool Overflow V8TOOLKIT_COMMA class Allocator){
    return v8::String::NewFromUtf8(isolate, value.c_str());
}


// CastToJS<eastl::vector<>>
template<class T, class... Rest>
struct CastToJS<eastl::vector<T, Rest...>> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector<T, Rest...> const & vector);
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector<T, Rest...> && vector) {
        return this->operator()(isolate, vector);
    }
};
template<class T, class... Rest>
struct CastToJS<eastl::vector<T, Rest...> const> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector<T, Rest...> const & vector) {
        return CastToJS<eastl::vector<T, Rest...>>()(isolate, vector);
    }
};


template<class T, class... Rest>
v8::Local<v8::Value>CastToJS<eastl::vector<T, Rest...>>::operator()(v8::Isolate *isolate, eastl::vector<T, Rest...> const & vector) {
    return cast_to_js_vector_helper<eastl::vector, T, Rest...>(isolate, vector);
}


template<class A, class B, class... Rest>
struct CastToJS<eastl::vector_multimap<A, B, Rest...>> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector_multimap<A, B, Rest...> const & multimap) {
        return casttojs_multimaplike(isolate, multimap);
    }
};

template<class A, class B, class... Rest>
struct CastToJS<eastl::vector_multimap<A, B, Rest...> const> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector_multimap<A, B, Rest...> const & multimap) {
        return casttojs_multimaplike(isolate, multimap);
    }
};


template<class T, class... Rest>
struct CastToJS<eastl::vector_set<T, Rest...>> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector_set<T, Rest...> const & set) {
        return cast_to_js_vector_helper(isolate, set);
    }
};

template<class T, class... Rest>
struct CastToJS<eastl::vector_set<T, Rest...> const> {
    v8::Local<v8::Value> operator()(v8::Isolate *isolate, eastl::vector_set<T, Rest...> const & set) {
        return cast_to_js_vector_helper(isolate, set);
    }
};





};

