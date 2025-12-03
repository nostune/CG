#pragma once

#include "RenderItem.h"
#include <vector>
#include <algorithm>

namespace outer_wilds {

class RenderQueue {
public:
    RenderQueue() = default;
    ~RenderQueue() = default;

    void Clear() { m_Items.clear(); }
    void Push(const RenderItem& item) { m_Items.push_back(item); }
    
    void Sort() {
        // Sort opaque front-to-back, transparent back-to-front
        std::sort(m_Items.begin(), m_Items.end(), [](const RenderItem& a, const RenderItem& b) {
            if (a.material->isTransparent && b.material->isTransparent) {
                return a.distanceToCamera > b.distanceToCamera; // Back-to-front for transparent
            }
            return a.distanceToCamera < b.distanceToCamera; // Front-to-back for opaque
        });
    }
    
    const std::vector<RenderItem>& GetItems() const { return m_Items; }

private:
    std::vector<RenderItem> m_Items;
};

} // namespace outer_wilds·
