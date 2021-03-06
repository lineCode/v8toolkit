#include "debugger.h"


#include <regex>
#include <v8-debug.h>

#include <websocketpp/endpoint.hpp>

#include <nlohmann/json.hpp>

namespace v8toolkit {

using namespace ::v8toolkit::literals;

using json = nlohmann::json;

int Debugger::STACK_TRACE_DEPTH = 10;


void Debugger::on_http(websocketpp::connection_hdl hdl) {
    Debugger::DebugServerType::connection_ptr con = this->debug_server.get_con_from_hdl(hdl);

    std::stringstream output;
    output << "<!doctype html><html><body>You requested "
           << con->get_resource()
           << "</body></html>";

    // Set status to 200 rather than the default error code
    con->set_status(websocketpp::http::status_code::ok);
    // Set body text to the HTML created above
    con->set_body(output.str());
}


void Debugger::helper(websocketpp::connection_hdl hdl) {
    std::string response;

//
//
//    this->debug_server.send(hdl, response, websocketpp::frame::opcode::TEXT);
//
//
//    response =
//    this->debug_server.send(hdl, response, websocketpp::frame::opcode::TEXT);

}


void Debugger::on_message(websocketpp::connection_hdl hdl, Debugger::DebugServerType::message_ptr msg) {
    std::smatch matches;
    std::string message = msg->get_payload();
//    std::cerr << "Got debugger message: " << message << std::endl;
    nlohmann::json json = json::parse(msg->get_payload());
    v8::Isolate *isolate = this->get_context().get_isolate();
    ISOLATE_SCOPED_RUN(isolate);

    if (std::regex_match(message, matches, std::regex("\\s*\\{\"id\":(\\d+),\"method\":\"([^\"]+)\".*"))) {
        int message_id = std::stoi(matches[1]);
        std::string method_name = matches[2];

        std::string response = fmt::format("{{\"id\":{},\"result\":{{}}}}", message_id);

        if (method_name == "Page.getResourceTree") {
//            std::cerr << "Message id for resource tree req: " << message_id << std::endl;
            this->debug_server.send(hdl, make_response(message_id, FrameResourceTree(*this)),
                                    websocketpp::frame::opcode::TEXT);
            this->debug_server.send(hdl, make_method(Runtime_ExecutionContextCreated(*this)),
                                    websocketpp::frame::opcode::TEXT);
            for (auto &script : this->get_context().get_scripts()) {
                this->debug_server.send(hdl, make_method(Debugger_ScriptParsed(*this, *script)),
                                        websocketpp::frame::opcode::TEXT);
            }
            for (auto & function : this->get_context().get_functions()) {
                this->send_message(make_method(Debugger_ScriptParsed(*this, function.Get(isolate))));

            }


        } else if (method_name == "Page.getResourceContent") {
            response = fmt::format("{{\"id\":{},{}}}", message_id, Page_Content("bogus content"));

            this->debug_server.send(hdl, response, websocketpp::frame::opcode::TEXT);

        } else if (method_name == "Debugger.getScriptSource") {
            // the URL for the script comes from Debugger.scriptParsed
//            std::cerr << msg->get_payload() << std::endl;
//            std::cerr << json << std::endl;
            std::string script_id_string = json["params"]["scriptId"];
            int64_t script_id = std::stol(script_id_string);
            for (auto &script : this->get_context().get_scripts()) {
                if (script->get_script_id() == script_id) {
                    this->debug_server.send(hdl, make_response(message_id, ScriptSource(*script)),
                                            websocketpp::frame::opcode::TEXT);
                    return;
                }
            }
            for (auto & function : this->get_context().get_functions()) {
                auto local_function = function.Get(isolate);
                if (local_function->GetScriptOrigin().ScriptID()->Value() == script_id) {
                    this->debug_server.send(hdl, make_response(message_id, ScriptSource(local_function)),
                                            websocketpp::frame::opcode::TEXT);
                    return;
                }
            }
        } else if (method_name == "Debugger.setBreakpointByUrl") {
            std::string url = json["params"]["url"];
            int line_number = json["params"]["lineNumber"];
//            std::cerr << fmt::format("set breakpoint on url: {} line: {}", url, line_number) << std::endl;

            v8toolkit::ScriptPtr script_to_break;
            int64_t script_id_to_break = -1;
            std::string script_location;
            for (v8toolkit::ScriptPtr const &script : context->get_scripts()) {
//                std::cerr << fmt::format("Comparing {} and {}", script->get_source_location(), url) << std::endl;
                if (script->get_source_location() == url) {
                    script_to_break = script;
                    script_id_to_break = script->get_script_id();
                    script_location = script->get_source_location();
                    break;
                }
            }
            // assert(script_to_break);

            if (!script_to_break) {
                for (v8::Global<v8::Function> const & global_function : context->get_functions()) {
                    v8::Local<v8::Function> function = global_function.Get(isolate);
                    v8::ScriptOrigin script_origin = function->GetScriptOrigin();
                    if (url == *v8::String::Utf8Value(script_origin.ResourceName())) {
                        script_id_to_break = script_origin.ScriptID()->Value();
                        script_location = *v8::String::Utf8Value(script_origin.ResourceName());
                    }
                }
            }

            auto debug_context = this->context->get_isolate_helper()->get_debug_context();
            v8::Context::Scope context_scope(*debug_context);
            v8::Local<v8::Value> result =
                    debug_context->run(
                            fmt::format("Debug.setScriptBreakPointById({}, {});", script_id_to_break,
                                        line_number)).Get(isolate);
//            std::cerr << v8toolkit::stringify_value(isolate, result) << std::endl;

            int64_t number = result->ToNumber()->Value();
//            auto result = debug_context->run(fmt::format("Debug.scripts();", url));
            //auto number = v8toolkit::call_simple_javascript_function(isolate, v8toolkit::get_key_as<v8::Function>(*debug_context, result->ToObject(), "number"));
            // auto number = v8toolkit::get_key_as(*debug_context, result->ToObject(), "")
            this->debug_server.send(hdl,
                                    make_response(message_id, Breakpoint(script_location, script_id_to_break, line_number)),
                                    websocketpp::frame::opcode::TEXT);

        } else if (method_name == "Runtime.evaluate") {
            auto json_params = json["params"];
            int context_id = json_params["contextId"];
            std::string expression = json_params["expression"];
            CONTEXT_SCOPED_RUN(this->get_context().get_context());
            auto result = this->get_context().run(expression);
            this->debug_server.send(hdl, make_response(message_id, RemoteObject(isolate, result.Get(isolate))),
                                    websocketpp::frame::opcode::TEXT);
        } else if (method_name == "Debugger.removeBreakpoint") {
            std::string breakpoint_url = json["params"]["breakpointId"];
            static std::regex breakpoint_id_regex("^(.*):(\\d+):(\\d+)$");
            std::smatch matches;
            if (!std::regex_match(breakpoint_url, matches, breakpoint_id_regex)) {
                assert(false);
            }
            std::string url = matches[1];
            int line_number = std::stoi(matches[2]);
            int column_number = std::stoi(matches[3]);

            // should probably call findBreakPoint
            // scriptBreakPoints() returns all breakpoints
            auto debug_context = this->context->get_isolate_helper()->get_debug_context();
            v8::Context::Scope context_scope(*debug_context);
            v8::Local<v8::Value> result =
                    debug_context->run(fmt::format(R"V0G0N(
                (
                        function(){{
                            var matching_breakpoints = 0;
                            var comparisons = "comparisons:";
                            var line_number = {};
                            var column_number = {};
                            let scripts = Debug.scriptBreakPoints().forEach(function(current_breakpoint){{
                                comparisons += ""+current_breakpoint.line() + line_number + current_breakpoint.column() + column_number;
                                if(current_breakpoint.line() == line_number && (!current_breakpoint.column() || current_breakpoint.column() == column_number)){{
                                    matching_breakpoints++;
                                    Debug.findBreakPoint(current_breakpoint.number(), true); // true disables the breakpoint
                                }}
                            }});
                            return matching_breakpoints;
                        }})();
        )V0G0N", line_number, column_number)).Get(isolate);
//            std::cerr << "breakpoints removed: " <<  v8toolkit::stringify_value(isolate, result) << std::endl;

            // result of scriptBreakPoints():
            // [
            //      {
            //          active_: true,
            //          break_points_: [
            //              {
            //                  active_: true,
            //                  actual_location: {
            //                  column: 4,
            //                  line: 4,
            //                  script_id: 55
            //                  },
            //              condition_: null,
            //              script_break_point_: ,
            //              source_position_: 46
            //                  }
            //          ], // end break_points
            //   column_: undefined,
            //   condition_: undefined,
            //   groupId_: undefined,
            //   line_: 4,
            //   number_: 1,
            //   position_alignment_: 0,
            //   script_id_: 55,
            //   type_: 0
            //               }, {...additional breakpoints...}
            // ]


            this->debug_server.send(hdl, make_response(message_id, ""), websocketpp::frame::opcode::TEXT);


        } else if (method_name == "Debugger.pause") {

        } else if (method_name == "Debugger.setSkipAllPauses") {

        } else if (method_name == "Debugger.setBreakpointsActive") {
            // active: true/false

        } else if (method_name == "Debugger.stepOver") {
            assert(this->paused_on_breakpoint); // should only get this while paused
            std::cout << "step over" << std::endl;

            auto debug_context = this->context->get_isolate_helper()->get_debug_context();
            v8::Context::Scope context_scope(*debug_context);

            v8::Local<v8::Value> step_type = debug_context->run("Debug.StepAction.StepNext").Get(isolate);
//            std::cerr << "About to try to call prepareStep on execution state: " << v8toolkit::stringify_value(isolate, this->breakpoint_execution_state.Get(isolate), true, true) << std::endl;
            v8toolkit::call_javascript_function(debug_context->get_context(), "prepareStep",
                                                this->breakpoint_execution_state.Get(isolate),
                                                std::make_tuple(step_type));
            this->resume_execution(); // allows breakpoint-hit loop in callback to exit

        } else if (method_name == "Debugger.stepInto") {
            assert(this->paused_on_breakpoint); // should only get this while paused
            std::cout << "step into" << std::endl;

            auto debug_context = this->context->get_isolate_helper()->get_debug_context();
            v8::Context::Scope context_scope(*debug_context);

            v8::Local<v8::Value> step_type = debug_context->run("Debug.StepAction.StepIn").Get(isolate);
            v8toolkit::call_javascript_function(debug_context->get_context(), "prepareStep",
                                                this->breakpoint_execution_state.Get(isolate),
                                                std::make_tuple(step_type));
            this->resume_execution(); // allows breakpoint-hit loop in callback to exit


        } else if (method_name == "Debugger.stepOut") {
            assert(this->paused_on_breakpoint); // should only get this while paused
            std::cout << "step out" << std::endl;

            auto debug_context = this->context->get_isolate_helper()->get_debug_context();
            v8::Context::Scope context_scope(*debug_context);

            v8::Local<v8::Value> step_type = debug_context->run("Debug.StepAction.StepOut").Get(isolate);
            v8toolkit::call_javascript_function(debug_context->get_context(), "prepareStep",
                                                this->breakpoint_execution_state.Get(isolate),
                                                std::make_tuple(step_type));
            this->resume_execution(); // allows breakpoint-hit loop in callback to exit


        } else if (method_name == "Debugger.resume") {
            assert(this->paused_on_breakpoint); // should only get this while paused
            std::cout << "resume" << std::endl;
            this->resume_execution();

        } else {
            this->debug_server.send(hdl, response, websocketpp::frame::opcode::TEXT);
        }


    } else {
        // unknown message format
        assert(false);
    }

}


    std::ostream &operator<<(std::ostream &os, const FrameResource &frame_resource) {
        os << fmt::format("{{\"url\":\"{}\",\"type\":\"{}\",\"mimeType\":\"{}\""/*,\"failed\":{},\"canceled\":{}*/"}}",
                          frame_resource.url, frame_resource.type,
                          frame_resource.mime_type/*, frame_resource.failed, frame_resource.canceled*/);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const Runtime_ExecutionContextDescription &context) {
        os << fmt::format("{{\"id\":{},\"isDefault\":{},\"name\":\"{}\",\"frameId\":\"{}\",\"origin\":\"{}\"}}",
                          context.execution_context_id, context.is_default, context.name, context.frame_id,
                          context.origin);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const Runtime_ExecutionContextCreated &context) {
        os << fmt::format("{{\"context\":{}}}", context.execution_context_description);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const FrameResourceTree &frame_resource_tree) {
        os << fmt::format("{{\"frameTree\":{{\"frame\":{}", frame_resource_tree.page_frame);
        os << ",\"childFrames\":[";
        bool first = true;
        for (auto &child_frame : frame_resource_tree.child_frames) {
            if (!first) {
                os << ",";
            }
            first = false;
            os << child_frame;
        }
        os << "],";
        os << "\"resources\":[";
        first = true;
        for (auto &resource : frame_resource_tree.resources) {
            if (!first) {
                os << ",";
            }
            first = false;
            os << resource;
        }
        os << "]}}";
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const ScriptSource &script_source) {
        os << fmt::format("{{\"scriptSource\":{}}}", script_source.source);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const Debugger_ScriptParsed &script_parsed) {
        os << fmt::format(
                "{{\"scriptId\":\"{}\",\"url\":\"{}\",\"startLine\":{},\"startColumn\":{}"/*,\"endLine\":{},\"endColumn\":{}*/"}}",
                script_parsed.script_id, script_parsed.url, script_parsed.start_line,
                script_parsed.start_column/*, script_parsed.end_line, script_parsed.end_column*/);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const Page_Content &content) {
        os << "\"result\":{";
        os << fmt::format("\"content\":\"{}\",\"base64Encoded\":{}", content.content,
                          (content.base64_encoded ? "true" : "false"));
        os << "}";
        return os;
    }



    std::ostream &operator<<(std::ostream &os, const RemoteObject &remote_object) {
        os << fmt::format("{{\"result\":{{\"type\":\"{}\",\"value\":{},\"description\":\"{}\"}},\"wasThrown\":{}}}",
                          remote_object.type, remote_object.value_string, remote_object.description,
                          remote_object.exception_thrown);
        return os;
    }


    std::ostream &operator<<(std::ostream &os, const Location &location) {
        os << fmt::format("{{\"scriptId\":\"{}\",\"lineNumber\":{},\"columnNumber\":{}}}",
                          location.script_id, location.line_number, location.column_number);
        return os;
    }



    std::ostream &operator<<(std::ostream &os, const PageFrame &page_frame) {
    os << fmt::format(
            "{{\"id\":\"{}\",\"parentId\":\"{}\",\"loaderId\":\"{}\",\"name\":\"{}\",\"url\":\"{}\",\"securityOrigin\":\"{}\",\"mimeType\":\"{}\"}}",
            page_frame.frame_id, page_frame.parent_id, page_frame.network_loader_id, page_frame.name,
            page_frame.url, page_frame.security_origin, page_frame.mime_type);
    return os;
}



    std::ostream &operator<<(std::ostream &os, const CallFrame &call_frame) {
//    {"method":"Debugger.paused","params":{"callFrames":[{"callFrameId":"{\"ordinal\":0,\"injectedScriptId\":20}","functionName":"","functionLocation":{"scriptId":"427","lineNumber":0,"columnNumber":0},"location":{"scriptId":"427","lineNumber":0,"columnNumber":0},"scopeChain":[{"type":"global","object":{"type":"object","className":"Window","description":"Window","objectId":"{\"injectedScriptId\":20,\"id\":1}"}}],"this":{"type":"object","className":"Window","description":"Window","objectId":"{\"injectedScriptId\":20,\"id\":2}"}}],"reason":"other","hitBreakpoints":["https://www.google-analytics.com/analytics.js:0:0"]}}
    os << fmt::format(
            "{{\"callFrameId\":\"{}\",\"functionName\":\"{}\",\"functionLocation\":{},\"location\":{},\"this\":{},\"scopeChain\":[{{\"type\":\"global\",\"object\":{{\"type\":\"object\",\"className\":\"Window\",\"description\":\"Window\",\"objectId\":\"{{\\\"injectedScriptId\\\":20,\\\"id\\\":1}}\"}}}}]}}",
            call_frame.call_frame_id, call_frame.function_name,
            call_frame.location, /*twice on purpose for testing */call_frame.location, call_frame.javascript_this);
    return os;
}


    std::ostream &operator<<(std::ostream &os, const Breakpoint &breakpoint) {
    std::stringstream locations;
    locations << "[";
    bool first = true;
    for (auto const &location : breakpoint.locations) {
        if (!first) {
            locations << ",";
        }
        first = false;
        locations << location;
    }
    locations << "]";

    os << fmt::format("{{\"breakpointId\":\"{}\",\"locations\":{}}}", breakpoint.breakpoint_id, locations.str());
    return os;
}


    RemoteObject::RemoteObject(v8::Isolate *isolate, v8::Local<v8::Value> value) {

    this->type = v8toolkit::get_type_string_for_value(value);
    this->value_string = v8toolkit::stringify_value(isolate, value);
//    this->subtype;
//    this->className;
    this->description = *v8::String::Utf8Value(value);
}


Page_Content::Page_Content(std::string const &content) : content(content) {}

FrameResource::FrameResource(Debugger const &debugger, v8toolkit::Script const &script) : url(
        script.get_source_location()) {}

Runtime_ExecutionContextDescription::Runtime_ExecutionContextDescription(Debugger const &debugger) :
        frame_id(debugger.get_frame_id()),
        origin(debugger.get_base_url()) {}

FrameResourceTree::FrameResourceTree(Debugger const &debugger) : page_frame(debugger) {
    // nothing to do for child frames at this point, it will always be empty for now

    for (auto &script : debugger.get_context().get_scripts()) {
        this->resources.emplace_back(FrameResource(debugger, *script));
    }
}

PageFrame::PageFrame(Debugger const &debugger) :
        frame_id(debugger.get_frame_id()),
        network_loader_id(debugger.get_frame_id()),
        security_origin(fmt::format("v8toolkit://{}", debugger.get_base_url())),
        url(fmt::format("v8toolkit://{}/", debugger.get_base_url())) {}

std::string const &Debugger::get_frame_id() const {
    return this->frame_id;
}

std::string Debugger::get_base_url() const {
    return this->context->get_uuid_string();
}

v8toolkit::Context &Debugger::get_context() const {
    return *this->context;
}

CallFrame::CallFrame(v8::Local<v8::StackFrame> stack_frame, v8::Isolate *isolate, v8::Local<v8::Value>) :
        function_name(*v8::String::Utf8Value(stack_frame->GetFunctionName())),
        location(stack_frame->GetScriptId(),
                 stack_frame->GetLineNumber() - 1 /* inspector.js always bumps it one line for unknown reason */),
        javascript_this(isolate, v8::Object::New(isolate)) {
}


Runtime_ExecutionContextCreated::Runtime_ExecutionContextCreated(Debugger const &debugger) :
        execution_context_description(debugger) {}

Debugger_ScriptParsed::Debugger_ScriptParsed(Debugger const &debugger, v8toolkit::Script const &script) :
        script_id(script.get_script_id()),
        url(script.get_source_location()) {}

Debugger_ScriptParsed::Debugger_ScriptParsed(Debugger const &debugger, v8::Local<v8::Function> const function) {
    auto isolate = debugger.get_context().get_isolate();
    auto function_origin = function->GetScriptOrigin();
    this->script_id = function_origin.ScriptID()->Value();
    this->url = *v8::String::Utf8Value(function_origin.ResourceName());
}
//    script_id(function->GetScriptOrigin().ScriptID()->Value()), url(function->GetScriptOrigin())


ScriptSource::ScriptSource(v8toolkit::Script const &script) :
    source(nlohmann::basic_json<>(script.get_source_code()).dump()) {}

ScriptSource::ScriptSource(v8::Local<v8::Function> function) :
    source(nlohmann::basic_json<>(*v8::String::Utf8Value(function)).dump()) {}


    Location::Location(int64_t script_id, int line_number, int column_number) :
        script_id(script_id),
        line_number(line_number),
        column_number(column_number) {}

Debugger_Paused::Debugger_Paused(Debugger const &debugger, v8::Local<v8::StackTrace> stack_trace, int64_t script_id,
                                 int line_number, int column_number) {

    try {

        v8toolkit::Script const &script = debugger.get_context().get_script_by_id(script_id);

        this->hit_breakpoints.emplace_back(
                fmt::format("\"{}:{}:{}\"", script.get_source_location(), line_number, column_number));
    } catch (v8toolkit::InvalidCallException &){}
    try {

        v8::Local<v8::Function> function = debugger.get_context().get_function_by_id(script_id);

        this->hit_breakpoints.emplace_back(
                fmt::format("\"{}:{}:{}\"", *v8::String::Utf8Value(function->GetScriptOrigin().ResourceName()), line_number, column_number));
    } catch (v8toolkit::InvalidCallException &){}





    // only doing 1 for now
    auto isolate = debugger.get_context().get_context()->GetIsolate();

    for (int i = 0; i < stack_trace->GetFrameCount(); i++) {
        this->call_frames.emplace_back(CallFrame(stack_trace->GetFrame(i), isolate, v8::Object::New(isolate)));
    }
}

Debugger_Paused::Debugger_Paused(Debugger const &debugger, v8::Local<v8::StackTrace> stack_trace) {
    // if no breakpoint was hit
    auto isolate = debugger.get_context().get_context()->GetIsolate();
    this->call_frames.emplace_back(CallFrame(stack_trace->GetFrame(0), isolate, v8::Object::New(isolate)));

}


Breakpoint::Breakpoint(std::string const & location, int64_t script_id, int line_number, int column_number) {
    this->breakpoint_id = fmt::format("{}:{}:{}", location, line_number, column_number);
    this->locations.emplace_back(Location(script_id, line_number, column_number));
}


    std::ostream &operator<<(std::ostream &os, const Debugger_Paused &paused) {
        /*
         {
             "method":"Debugger.paused",
             "params":{
                "callFrames":[
                    {
                        "callFrameId":"{\"ordinal\":0,\"injectedScriptId\":2}",
                         "functionName":"",
                         "functionLocation":{"scriptId":"70","lineNumber":0,"columnNumber":38},
                         "location":{"scriptId":"70","lineNumber":1,"columnNumber":0},
                         "scopeChain":[
                            {
                                "type":"local",
                                "object":{
                                    "type":"object",
                                    "className":"Object",
                                    "description":"Object",
                                    "objectId":"{\"injectedScriptId\":2,\"id\":1}"
                                },
                                "startLocation":{
                                    "scriptId":"70",
                                    "lineNumber":0,
                                    "columnNumber":38
                                },
                                "endLocation":{
                                    "scriptId":"70",
                                    "lineNumber":517,
                                    "columnNumber":126
                                }
                            },
                            {"type":"global","object":{"type":"object","className":"Window","description":"Window","objectId":"{\"injectedScriptId\":2,\"id\":2}"}}
                        ],
                        "this":{
                            "type":"object",
                            "className":"Window",
                            "description":"Window",
                            "objectId":"{\"injectedScriptId\":2,\"id\":3}"
                        }
                    },
                    // another call frame on this line, same as above
                    {"callFrameId":"{\"ordinal\":1,\"injectedScriptId\":2}","functionName":"","functionLocation":{"scriptId":"70","lineNumber":0,"columnNumber":0},"location":{"scriptId":"70","lineNumber":517,"columnNumber":127},"scopeChain":[{"type":"global","object":{"type":"object","className":"Window","description":"Window","objectId":"{\"injectedScriptId\":2,\"id\":4}"}}],"this":{"type":"object","className":"Window","description":"Window","objectId":"{\"injectedScriptId\":2,\"id\":5}"}}
                ], // end callFrames
                "reason":"other",
                "hitBreakpoints":[
                    "https://ssl.gstatic.com/sites/p/2a2c4f/system/js/jot_min_view__en.js:1:0"
                ] // end hitBreakpoints
            } // end params
         } // end message
         */
        // callFrames array should be populated, but not implemented yet, don't know how, not sure if absolutely req'd
        os << fmt::format("{{\"callFrames\":[],\"reason\":\"{}\",\"hitBreakpoints\":[", paused.reason);
        bool first = true;
        for (auto const &breakpoint : paused.hit_breakpoints) {
            if (!first) {
                os << ",";
            }
            first = false;
            os << breakpoint;
        }
        os << "],\"callFrames\":[";
        first = true;
        for (auto const &call_frame : paused.call_frames) {
            if (!first) {
                os << ",";
            }
            first = false;
            os << call_frame;
        }

        os << "]}";
        return os;
    }

    std::ostream &operator<<(std::ostream &os, const Debugger_Resumed &resumed) {
        assert(false);
    }



    /**
* Returning from this function resumes javascript execution
*/
void Debugger::debug_event_callback(v8::Debug::EventDetails const &event_details) {


        // reverted to old version of v8, so had to change this line
//    v8::Isolate *isolate = event_details.GetIsolate();
        v8::Isolate *isolate = v8::Isolate::GetCurrent(); // this line may now be wrong - I just want it to compile

        v8::Local<v8::Context> debug_context = v8::Debug::GetDebugContext(isolate);
    DebugEventCallbackData *callback_data =
            static_cast<DebugEventCallbackData *>(v8::External::Cast(*event_details.GetCallbackData())->Value());
    Debugger &debugger = *callback_data->debugger;

//    std::cerr << "GOT DEBUG EVENT CALLBACK WITH EVENT TYPE " << event_details.GetEvent() << std::endl;

    v8::Local<v8::Object> event_data = event_details.GetEventData();
//    std::cerr << "event data: " << std::endl << v8toolkit::stringify_value(isolate, event_data, true, true)
//              << std::endl;

    v8::Local<v8::Object> execution_state = event_details.GetExecutionState();
//    debugger.breakpoint_execution_state.Reset(isolate, execution_state);

//    std::cerr << "execution state: " << std::endl
//              << v8toolkit::stringify_value(isolate, execution_state, true, true) << std::endl;
//
//    std::cerr << "end printing event callback data" << std::endl;

    v8::DebugEvent debug_event_type = event_details.GetEvent();

    // If this notification corresponds to a breakpoint being hit
    if (debug_event_type == v8::DebugEvent::Break) {
//        std::cerr << v8toolkit::stringify_value(debug_context->GetIsolate(), event_data, true, true) << std::endl;

        v8::Local<v8::Value> str = "break_points_hit_"_v8;
//        std::cerr << v8toolkit::stringify_value(debug_context->GetIsolate(), event_data->Get(str), true, true) << std::endl;


        // TEST CODE FOR STACKTRACE LEARNING
        v8::Local<v8::StackTrace> stack_trace = v8::StackTrace::CurrentStackTrace(isolate,
                                                                                  Debugger::STACK_TRACE_DEPTH,
                                                                                  static_cast<v8::StackTrace::StackTraceOptions>(
                                                                                          v8::StackTrace::kOverview |
                                                                                          v8::StackTrace::kScriptId));
        v8::Local<v8::StackFrame> stack_frame = stack_trace->GetFrame(0);
        std::cerr << stack_frame->GetLineNumber() << " : " << *v8::String::Utf8Value(stack_frame->GetScriptName())
                  << " : " << *v8::String::Utf8Value(stack_frame->GetFunctionName()) << std::endl;


        try {
            v8::Local<v8::Array> hit_breakpoints_array = v8toolkit::get_key_as<v8::Array>(debug_context, event_data,
                                                                                          "break_points_hit_");

            assert(hit_breakpoints_array->Length() == 1); // don't know how to handle anything else
            v8::Local<v8::Value> breakpoint_info = hit_breakpoints_array->Get(debug_context, 0).ToLocalChecked();
            v8::Local<v8::Value> breakpoint_location = v8toolkit::get_key(debug_context, breakpoint_info,
                                                                          "actual_location");
            int64_t script_id = v8toolkit::get_key_as<v8::Number>(debug_context, breakpoint_location,
                                                                  "script_id")->Value();
            int64_t line_number = v8toolkit::get_key_as<v8::Number>(debug_context, breakpoint_location,
                                                                    "line")->Value();
            int64_t column_number = v8toolkit::get_key_as<v8::Number>(debug_context, breakpoint_location,
                                                                      "column")->Value();

            v8::Local<v8::Object> script_break_point = v8toolkit::get_key_as<v8::Object>(debug_context,
                                                                                         breakpoint_info,
                                                                                         "script_break_point_");
            debugger.breakpoint_paused_on = v8toolkit::get_key_as<v8::Number>(debug_context, script_break_point,
                                                                              "number_")->Value();



            // send message to debugger notifying that breakpoint was hit
            callback_data->debugger->send_message(
                    make_method(Debugger_Paused(debugger, stack_trace, script_id, line_number, column_number)));
        } catch (v8toolkit::UndefinedPropertyException &) {

            // no breakpoint hit
            callback_data->debugger->send_message(
                    make_method(Debugger_Paused(debugger, stack_trace)));
        }

        // cannot handle hitting a breakpoint while stopped at a breakpoint
        //   not sure what that would even mean
        assert(debugger.paused_on_breakpoint == false);
        debugger.breakpoint_execution_state.Reset(isolate, execution_state);
        debugger.paused_on_breakpoint = true;
        int loop_counter = 0;
        while (debugger.paused_on_breakpoint) {
            debugger.debug_server.poll_one();
            usleep(250000);
            if (++loop_counter % 40 == 0) {
//                printf("Still waiting for resume from break command from debugger afater %d second\n", loop_counter / 4);
            }
        }
    }
    printf("Resuming from breakpoint\n");

    /* GetEventData() when a breakpoint is hit returns:
     * {
     *      break_points_hit_:
         *      [
             *      {
                 *      active_: true, actual_location: {column: 4, line: 13, script_id: 55}, condition_: null,
                 *      script_break_point_: {
                 *          active_: true,
                 *          break_points_: [],
                 *          column_: undefined,
                 *          condition_: undefined,
                 *          groupId_: undefined,
                 *          line_: 13,
                 *          number_: 1,
                 *          position_alignment_: 0,
                 *          script_id_: 55,
                 *          type_: 0
             *          },
             *          source_position_: 175
         *          }
     *          ],
     *          frame_: {
     *              break_id_: 8,
     *              details_: {
     *                  break_id_: 8,
     *                  details_: [
     *                      392424216, {}, function a(){
                                println("Beginning of a()");
                                let some_var = 5;
                                some_var += 5;
                                b(some_var);
                                println("End of a()");
                            },
                            {
                                sourceColumnStart_: [undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, 4, undefined, undefined, undefined, undefined, undefined, undefined, undefined]
                            }, 0, 1, 175, false, false, 0, "some_var", 5
                        ]
                },
                index_: 0,
                type_: "frame"
            }
        }

     */

}

void Debugger::on_open(websocketpp::connection_hdl hdl) {
    assert(this->connections.size() == 0);
    this->connections.insert(hdl);
}


void Debugger::on_close(websocketpp::connection_hdl hdl) {
    assert(this->connections.size() == 1);
    this->connections.erase(hdl);
    assert(this->connections.size() == 0);

}


bool Debugger::websocket_validation_handler(websocketpp::connection_hdl hdl) {
    // only allow one connection
    return this->connections.size() == 0;
}

void Debugger::send_message(std::string const &message) {
    if (!this->connections.empty()) {
        this->debug_server.send(*this->connections.begin(), message, websocketpp::frame::opcode::TEXT);
    }
}

void Debugger::resume_execution() {
    this->breakpoint_execution_state.Reset();
    this->breakpoint_paused_on = -1;
    this->paused_on_breakpoint = false;
}


Debugger::Debugger(v8toolkit::ContextPtr &context,
                   unsigned short port) :
        context(context),
        port(port) {
    // disables all websocketpp debugging messages
    //websocketpp::set_access_channels(websocketpp::log::alevel::none);
    this->debug_server.get_alog().clear_channels(websocketpp::log::alevel::all);
    this->debug_server.get_elog().clear_channels(websocketpp::log::elevel::all);

    v8::Isolate *isolate = context->get_isolate();

    GLOBAL_CONTEXT_SCOPED_RUN(isolate, context->get_global_context());
    v8toolkit::add_print(v8::Debug::GetDebugContext(isolate));


    auto data = v8::External::New(isolate, new DebugEventCallbackData(this));
    v8::Debug::SetDebugEventListener(context->get_isolate(), debug_event_callback, data);

    // only allow one connection
    this->debug_server.set_validate_handler(
            bind(&Debugger::websocket_validation_handler, this, websocketpp::lib::placeholders::_1));

    // store the connection so events can be sent back to the client without the client sending something first
    this->debug_server.set_open_handler(bind(&Debugger::on_open, this, websocketpp::lib::placeholders::_1));

    // note that the client disconnected
    this->debug_server.set_close_handler(bind(&Debugger::on_close, this, websocketpp::lib::placeholders::_1));

    // handle websocket messages from the client
    this->debug_server.set_message_handler(bind(&Debugger::on_message, this, websocketpp::lib::placeholders::_1,
                                                websocketpp::lib::placeholders::_2));
    this->debug_server.set_open_handler(
            websocketpp::lib::bind(&Debugger::on_open, this, websocketpp::lib::placeholders::_1));
    this->debug_server.set_http_handler(
            websocketpp::lib::bind(&Debugger::on_http, this, websocketpp::lib::placeholders::_1));

    this->debug_server.init_asio();
    this->debug_server.set_reuse_addr(true);
    std::cerr << "Listening on port " << this->port << std::endl;
    this->debug_server.listen(this->port);

    this->debug_server.start_accept();
}

void Debugger::poll() {
    this->debug_server.poll();
}

void Debugger::poll_one() {
    this->debug_server.poll_one();
}


using namespace std::literals::chrono_literals;

void Debugger::wait_for_connection(std::chrono::duration<float> sleep_between_polls) {
    while(this->connections.empty()) {
        this->poll_one();
        if (this->connections.empty()) {
            // don't suck up 100% cpu while waiting for connection
            std::this_thread::sleep_for(sleep_between_polls);
        }
    }
}


}