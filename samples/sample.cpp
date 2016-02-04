#include <iostream>
#include <fstream>

#include <stdio.h>

#define USE_BOOST
#include "v8_class_wrapper.h"

using namespace v8toolkit;
using namespace std;

#define SAMPLE_DEBUG true

struct Foo {
    Foo(){if (SAMPLE_DEBUG) printf("Created Foo %p (default constructor)\n", this);}
    Foo(const Foo &){if (SAMPLE_DEBUG) printf("Foo copy constructor\n");}
    ~Foo(){if (SAMPLE_DEBUG) printf("deleted Foo %p\n", this);}
    int i = 42;
};

// random sample class for wrapping - not actually a part of the library
class Point {
public:
    Point() : x_(69), y_(69) {instance_count++; if (SAMPLE_DEBUG) printf("created Point (default constructor)\n");}
    Point(int x, int y) : x_(x), y_(y) { instance_count++; if (SAMPLE_DEBUG) printf("created Point with 2 ints\n");}
    Point(const Point & p) {instance_count++; assert(false); /* This is to help make sure none of the helpers are creating copies */ }
    ~Point(){instance_count--; if (SAMPLE_DEBUG) printf("Point destructor called on %p\n", this);}
    int x_, y_;
    int thing(int z, char * zz){if (SAMPLE_DEBUG) printf("In Point::Thing with this %p x: %d y: %d and input value %d %s\n", this, this->x_, this->y_, z, zz); return z*2;}
    int overloaded_method(char * foo){return 0;}
    int overloaded_method(int foo){return 1;}
    const char * stringthing() {return "hello";}
    void void_func() {}
    
    // returns a new point object that should be managed by V8 GC
    Point * make_point(){return new Point();}
    
    // Foo & get_foo(Foo & f)  {return f;}
    
    // Leave this as an r-value return for testing purposes Foo f;
    Foo get_foo() {return Foo();}
    
    static int get_instance_count(){
        printf("Point::get_instance_count: %d\n", Point::instance_count);
        return instance_count;
    }
    static int instance_count;
};

int Point::instance_count = 0;


struct Line {
    Line(){if (SAMPLE_DEBUG) printf("Created line %p (default constructor)\n", this);}
    ~Line(){if (SAMPLE_DEBUG) printf("Deleted line %p\n", this);}
    Point p;
    Point & get_point(){return this->p;}
    Point get_rvalue_point(){return Point();}
    void some_method(int){}
};



void print_maybe_value(v8::MaybeLocal<v8::Value> maybe_value) 
{
    if (maybe_value.IsEmpty()) {
        printf("Maybe value was empty\n");
    } else {
        auto local = maybe_value.ToLocalChecked();
        v8::String::Utf8Value utf8(local);
        printf("Maybe value: '%s'\n", *utf8);
    }
}

void some_function(int){}

int main(int argc, char* argv[]) 
{

    // parse out any v8-specific command line flags
    process_v8_flags(argc, argv);
    expose_gc(); // force garbage collection to be exposed even if no command line parameter for it
        
    // Initialize V8.
    v8::V8::InitializeICU();
#ifdef USE_SNAPSHOTS
    v8::V8::InitializeExternalStartupData(argv[0]);
#endif
    v8::Platform* platform = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform);
    v8::V8::Initialize();

    // Create a new Isolate and make it the current one.
    ArrayBufferAllocator allocator;
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = &allocator;
    v8::Isolate* isolate = v8::Isolate::New(create_params);
    {
        scoped_run(isolate, [&](){
            // how to expose global variables as javascript variables "x" and "y"
            // global_templ->SetAccessor(String::NewFromUtf8(isolate, "x"), XGetter, XSetter);
            // global_templ->SetAccessor(String::NewFromUtf8(isolate, "y"), YGetter, YSetter);

            // wrap the constructor and add it to the global template
            // Local<FunctionTemplate> ft = FunctionTemplate::New(isolate, create);
            v8::Local<v8::ObjectTemplate> global_templ = v8::ObjectTemplate::New(isolate);
        
            add_print(isolate, global_templ);
            
            add_function(isolate, global_templ, "some_function", some_function);

            // // add the function "four()" to javascript
            // global_templ->Set(v8::String::NewFromUtf8(isolate, "four"), FunctionTemplate::New(isolate, four));

            // make the Point constructor function available to JS
            auto & wrapped_point = V8ClassWrapper<Point>::get_instance(isolate);
            wrapped_point.add_constructor("Point", global_templ);
            wrapped_point.add_constructor("SameAsPoint", global_templ); // in case you want to have the same constructor in two places
            wrapped_point.add_constructor<int,int>("Pii", global_templ);
            wrapped_point.add_method(&Point::thing, "thing");
            add_function(isolate, global_templ, "point_instance_count", &Point::get_instance_count);
        

            // overloaded functions can be individually addressed, but they can't be the same name to javascript
            //   at least not without some serious finagling of storing a mapping between a singlne name and
            //   multiple function templates as well as some sort of "closeness" function for determining
            //   which primitive type parameters most closely match the javascript values provided
            wrapped_point.add_method<int, char*>(&Point::overloaded_method, "overloaded_method1");
            wrapped_point.add_method<int, int>(&Point::overloaded_method, "overloaded_method2");
            wrapped_point.add_method(&Point::make_point, "make_point");

            wrapped_point.add_method(&Point::stringthing, "stringthing").add_method(&Point::void_func, "void_func");
            wrapped_point.add_member(&Point::x_, "x");
            wrapped_point.add_member(&Point::y_, "y");
        
            // if you register a function that returns an r-value, a copy will be made using the copy constsructor
            wrapped_point.add_method(&Point::get_foo, "get_foo");
        
            auto & wrapped_line = V8ClassWrapper<Line>::get_instance(isolate);
            wrapped_line.add_constructor("Line", global_templ);
            wrapped_line.add_method(&Line::get_point, "get_point");
            wrapped_line.add_method(&Line::get_rvalue_point, "get_rvalue_point");
            wrapped_line.add_member(&Line::p, "p");
        
            auto & wrapped_foo = V8ClassWrapper<Foo>::get_instance(isolate);
            wrapped_foo.add_member(&Foo::i, "i");
        
            v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global_templ);
            v8::Context::Scope context_scope_x(context);
            

            auto js_code = get_file_contents("code.js");
            v8::Local<v8::String> source =
                v8::String::NewFromUtf8(isolate, js_code.c_str(),
                                    v8::NewStringType::kNormal).ToLocalChecked();

            // Compile the source code.
            v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();

            printf("About to start running script\n");
            auto result = script->Run(context);
            print_maybe_value(result);

            std::vector<std::string> v{"hello", "there", "this", "is", "a", "vector"};
            add_variable(context, context->Global(), "v", CastToJS<decltype(v)>()(isolate, v));
            std::list<float> l{1.5, 2.5, 3.5, 4.5};
            add_variable(context, context->Global(), "l", CastToJS<decltype(l)>()(isolate, l));
            std::map<std::string, int> m{{"one", 1},{"two", 2},{"three", 3}};
            add_variable(context, context->Global(), "m", CastToJS<decltype(m)>()(isolate, m));
            std::map<std::string, int> m2{{"four", 4},{"five", 5},{"six", 6}};
            add_variable(context, context->Global(), "m2", CastToJS<decltype(m2)>()(isolate, m2));
            std::deque<long> d{7000000000, 8000000000, 9000000000};
            add_variable(context, context->Global(), "d", CastToJS<decltype(d)>()(isolate, d));
            std::multimap<int, int> mm{{1,1},{1,2},{1,3},{2,4},{3,5},{3,6}};
            add_variable(context, context->Global(), "mm", CastToJS<decltype(mm)>()(isolate, mm));
            std::array<int, 3> a{{1,2,3}};
            add_variable(context, context->Global(), "a", CastToJS<decltype(a)>()(isolate, a));
            
            std::map<string, vector<int>> composite = {{"a",{1,2,3}},{"b",{4,5,6}},{"c",{7,8,9}}};
            add_variable(context, context->Global(), "composite", CastToJS<decltype(composite)>()(isolate, composite));
            
            v8::Local<v8::String> source2 =
                v8::String::NewFromUtf8(isolate, get_file_contents("sample2.js").c_str());
            v8::Local<v8::Script> script2 = v8::Script::Compile(context, source2).ToLocalChecked();
            (void)script2->Run(context);

            // throwing a c++ exception here immediately terminates the process
            // add_function(context, context->Global(), "throw_exception", [](){throw std::exception();});
            printf("Checking that calling a normal function with too few parameters throws\n");
            v8::Local<v8::Script> script3 = v8::Script::Compile(context, v8::String::NewFromUtf8(isolate,"some_function();")).ToLocalChecked();
            v8::TryCatch tc(isolate);
            try{
                (void)script3->Run(context);
                if(tc.HasCaught()){
                    printf("TC has caught\n");
                } else {
                    printf("TC has not caught\n");
                }
            } catch(...) {
                printf("Caught in regular trycatch\n");
            }
            
            printf("Checking that calling a class method with too few parameters throws\n");
            v8::Local<v8::Script> script4 = v8::Script::Compile(context, v8::String::NewFromUtf8(isolate,"l=new Line();l.some_method();")).ToLocalChecked();
            v8::TryCatch tc2(isolate);
            try{
                (void)script4->Run(context);
                if(tc2.HasCaught()){
                    printf("tc2 has caught\n");
                } else {
                    printf("tc2 has not caught\n");
                }
            } catch(...) {
                printf("Caught in regular trycatch\n");
            }
            
                        
        });
        
    }

    // Dispose the isolate and tear down V8.
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete platform;
    return 0;
}


    // an Isolate is a V8 instance where multiple applications can run at the same time, but only 
    //   on thread can be running an Isolate at a time.  

    // an context represents the resources needed to run a javascript program
    //   if a program monkey patches core javascript functionality in one context it won't be 
    //   visible to another context
    //   Local<Context> context = Context::New(isolate);
    //   A context has a global object template, but function templates can be added to it

    // A handle is a reference to a javascript object and while active will stop the object from being 
    //   garbage collected
    //   handles exist within a stack-only allocated handle scope. (cannot new() a handle scope) 
    //   UniquePersistent handle is like a unique_ptr
    //   Persistent handle is must be released manually with ::Reset() method

    // EscapableHandleScope lets you return a handle scope created inside a function, otherwise
    //   all handles created in that function will be destroyed before a value is returned
    //   Return with: return handle_scope.Escape(array);

    // Templates allow c++ functions and objects to be made visible in javascript.
    //   templates are created within a context and must be created in each context they are to be used in
    //   Templates have accessors and interceptors
    //      accessors are tied to specific field names
    //      interceptors are called on ALL field name gets/sets (either by name foo.bar or by index as in foo[2])

    // Templates can have prototype templates to simulate prototypical inheritance


