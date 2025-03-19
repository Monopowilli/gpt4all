#pragma once

#include <QtTypes>


namespace gpt4all::backend {


struct GenerationParams {
    uint  n_predict;
    float temperature;
    float top_p;
    // int32_t top_k = 40;
    // float   min_p = 0.0f;
    // int32_t n_batch = 9;
    // float   repeat_penalty = 1.10f;
    // int32_t repeat_last_n = 64;     // last n tokens to penalize
    // float   contextErase = 0.5f;    // percent of context to erase if we exceed the context window
};


} // namespace gpt4all::backend
