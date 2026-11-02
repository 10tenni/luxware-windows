#pragma once
#define ORT_API_MANUAL_INIT
#include "platform.hpp"
#if defined(__MINGW32__) && !defined(_stdcall)
#define _stdcall __stdcall
#endif
#include "onnxruntime_cxx_api.h"

namespace lux {
class Model {
  HMODULE library = nullptr;
  std::unique_ptr<Ort::Env> env;
  std::unique_ptr<Ort::Session> session;
  std::string inputName, outputName;
  Tensorizer preprocessor;

public:
  std::string provider = "Not loaded";
  ~Model() {
    session.reset();
    env.reset();
    if (library)
      FreeLibrary(library);
  }
  void load(const std::filesystem::path &model, bool gpu) {
    session.reset();
    if (!library) {
      auto path = appFolder() / L"onnxruntime.dll";
      library = LoadLibraryExW(path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      if (!library)
        throw std::runtime_error("ONNX Runtime could not load (Windows error " +
                                 std::to_string(GetLastError()) +
                                 "). Keep all DLLs beside Luxware.exe. If dependencies are missing, install "
                                 "Prerequisites/VC_redist.x64.exe.");
      using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
      auto get = reinterpret_cast<GetApiBase>(GetProcAddress(library, "OrtGetApiBase"));
      if (!get)
        throw std::runtime_error("Wrong ONNX Runtime DLL");
      auto api = get()->GetApi(ORT_API_VERSION);
      if (!api)
        throw std::runtime_error("ONNX Runtime API version mismatch");
      Ort::InitApi(api);
      env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "Luxware");
    }
    auto options = []() {
      Ort::SessionOptions o;
      o.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      o.SetIntraOpNumThreads(std::max(1, std::min(4, int(std::thread::hardware_concurrency() / 2))));
      o.SetInterOpNumThreads(1);
      o.AddConfigEntry("session.intra_op.allow_spinning", "0");
      o.AddConfigEntry("session.inter_op.allow_spinning", "0");
      o.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      return o;
    };
    std::string fallback;
    if (gpu) {
      using Append = OrtStatus *(ORT_API_CALL *)(OrtSessionOptions *, int);
      auto append =
          reinterpret_cast<Append>(GetProcAddress(library, "OrtSessionOptionsAppendExecutionProvider_DML"));
      if (append)
        try {
          auto o = options();
          o.DisableMemPattern();
          Ort::ThrowOnError(append(o, 0));
          session = std::make_unique<Ort::Session>(*env, model.c_str(), o);
          provider = "DirectML GPU";
        } catch (const std::exception &e) {
          fallback = e.what();
          session.reset();
        }
      else
        fallback = "DirectML provider unavailable";
    }
    if (!session) {
      auto o = options();
      session = std::make_unique<Ort::Session>(*env, model.c_str(), o);
      provider = gpu ? "CPU (DirectML unavailable)" : "CPU";
    }
    if (session->GetInputCount() != 1 || session->GetOutputCount() != 1)
      throw std::runtime_error("Expected one image input and one decoded YOLO output");
    auto inputType = session->GetInputTypeInfo(0);
    auto input = inputType.GetTensorTypeAndShapeInfo();
    auto shape = input.GetShape();
    if (input.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        shape != std::vector<int64_t>({1, 3, 640, 640}))
      throw std::runtime_error("Expected float32 RGB [1,3,640,640] input");
    auto outputType = session->GetOutputTypeInfo(0);
    auto output = outputType.GetTensorTypeAndShapeInfo();
    auto outShape = output.GetShape();
    if (output.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || outShape.size() != 3 ||
        outShape[0] != 1 || (outShape[1] != 5 && outShape[2] != 5))
      throw std::runtime_error("Expected single-class decoded YOLO [1,5,N] output. Generic human models/raw "
                               "other formats are unsupported.");
    Ort::AllocatorWithDefaultOptions allocator;
    inputName = session->GetInputNameAllocated(0, allocator).get();
    outputName = session->GetOutputNameAllocated(0, allocator).get();
  }
  std::vector<Detection> run(const Image &image, double confidence) {
    if (!session)
      throw std::runtime_error("Model not loaded");
    auto &input = preprocessor.prepare(image);
    std::array<int64_t, 4> dims{1, 3, 640, 640};
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto value =
        Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), dims.data(), dims.size());
    const char *in = inputName.c_str();
    const char *out = outputName.c_str();
    auto values = session->Run(Ort::RunOptions{nullptr}, &in, &value, 1, &out, 1);
    auto shape = values[0].GetTensorTypeAndShapeInfo().GetShape();
    return decode(values[0].GetTensorData<float>(), shape, confidence);
  }
};
} // namespace lux
