#ifndef VW_BERGAMOT_ENGINE_H_
#define VW_BERGAMOT_ENGINE_H_

#include <memory>
#include <string>

namespace vw::spike {

class BergamotEngine {
 public:
  // Loads one Bergamot translation model from the supplied YAML configuration and owns its resident inference service
  // for the lifetime of this adapter.
  explicit BergamotEngine(const std::string& model_config_path);

  // Releases the resident Bergamot model and inference service owned exclusively by this adapter instance.
  ~BergamotEngine();

  BergamotEngine(const BergamotEngine&) = delete;
  BergamotEngine& operator=(const BergamotEngine&) = delete;

  // Transfers exclusive ownership of the loaded Bergamot model and service without reloading model files.
  BergamotEngine(BergamotEngine&&) noexcept;

  // Replaces this adapter's owned Bergamot state by moving another instance without copying or reloading the model.
  BergamotEngine& operator=(BergamotEngine&&) noexcept;

  // Translates one UTF-8 subtitle cue synchronously with the resident local model and returns its translated UTF-8 text.
  std::string translate(const std::string& text);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vw::spike

#endif  // VW_BERGAMOT_ENGINE_H_
