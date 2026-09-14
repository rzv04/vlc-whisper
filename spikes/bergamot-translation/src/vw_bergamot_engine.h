#ifndef VW_BERGAMOT_ENGINE_H_
#define VW_BERGAMOT_ENGINE_H_

#include <memory>
#include <string>

namespace vw::spike {

class BergamotEngine {
 public:
  explicit BergamotEngine(const std::string& model_config_path);
  ~BergamotEngine();

  BergamotEngine(const BergamotEngine&) = delete;
  BergamotEngine& operator=(const BergamotEngine&) = delete;
  BergamotEngine(BergamotEngine&&) noexcept;
  BergamotEngine& operator=(BergamotEngine&&) noexcept;

  std::string translate(const std::string& text);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vw::spike

#endif  // VW_BERGAMOT_ENGINE_H_
