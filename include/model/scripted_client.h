// include/model/scripted_client.h

#pragma once
#include "model/model_client.h"
#include <string>
#include <vector>
#include <stdexcept>

class ScriptedModelClient : public ModelClient {
public:
    using ModelClient::generate;

    explicit ScriptedModelClient(const std::string& script_path);

    void generate(const Conversation& conv, TokenSink& sink) override;

private:
    struct ScriptBlock {
        std::string content;
        int chunk_size = -1; // -1 means emit all at once
    };

    std::vector<ScriptBlock> blocks_;
    std::size_t current_block_idx_ = 0;
};
