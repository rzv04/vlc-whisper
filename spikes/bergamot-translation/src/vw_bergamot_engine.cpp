#include "vw_bergamot_engine.h"

#include <stdexcept>
#include <utility>
#include <vector>

#include "translator/parser.h"
#include "translator/response_options.h"
#include "translator/service.h"
#include "translator/translation_model.h"

namespace vw::spike {

struct BergamotEngine::Impl {
  explicit Impl(const std::string& model_config_path)
      : service(marian::bergamot::BlockingService::Config{}) {
    auto options = marian::bergamot::parseOptionsFromFilePath(model_config_path);
    if (!options) throw std::runtime_error("Bergamot could not parse the model config");
    model = std::make_shared<marian::bergamot::TranslationModel>(options);
    response_options.HTML = true;
    response_options.concatStrategy = marian::bergamot::ConcatStrategy::FAITHFUL;
  }

  marian::bergamot::BlockingService service;
  std::shared_ptr<marian::bergamot::TranslationModel> model;
  marian::bergamot::ResponseOptions response_options;
};

BergamotEngine::BergamotEngine(const std::string& model_config_path)
    : impl_(std::make_unique<Impl>(model_config_path)) {}

BergamotEngine::~BergamotEngine() = default;
BergamotEngine::BergamotEngine(BergamotEngine&&) noexcept = default;
BergamotEngine& BergamotEngine::operator=(BergamotEngine&&) noexcept = default;

std::string BergamotEngine::translate(const std::string& text) {
  if (text.empty()) return {};

  std::vector<std::string> sources;
  sources.push_back(text);
  std::vector<marian::bergamot::ResponseOptions> options = {impl_->response_options};
  auto responses = impl_->service.translateMultiple(impl_->model, std::move(sources), options);
  if (responses.size() != 1) throw std::runtime_error("Bergamot returned an unexpected response count");
  if (responses[0].target.text.empty()) throw std::runtime_error("Bergamot returned an empty translation");
  return responses[0].target.text;
}

}  // namespace vw::spike
