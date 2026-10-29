#include <iostream>
#include "platform/windows/account/avatar.h"
#include <cstdlib>
using namespace lume::account;
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
int main() {
    const auto png = Unbase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8_x8AAwMCAO-aXx8AAAAASUVORK5CYII");
    auto image = ValidateAvatar(png);
    Expect(image.mime == "image/png", "native PNG decoder");
    Expect(ValidateAvatar(Unbase64("UklGRh4AAABXRUJQVlA4TBEAAAAvAUAAAAdQ0EKVu_-BiOh_AAA")).mime == "image/webp",
        "real lossless WebP fixture accepted");
    const auto jpeg = Unbase64(
        "_9j_4AAQSkZJRgABAQAAAQABAAD_2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL_"
        "2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL_wAARCAACAAIDASIAAhEBAxEB_8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL_"
        "8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4-Tl5ufo6erx8vP09fb3-Pn6_"
        "8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL_8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3-Pn6_"
        "9oADAMBAAIRAxEAPwC_RRRX0x8uf__Z");
    Expect(ValidateAvatar(jpeg).mime == "image/jpeg", "real JPEG fixture accepted");
    const auto url = AvatarDataUrl(image);
    Expect(url.find("data:image/png;base64,") == 0 && url.size() < 3 * MaxAvatarBytes, "restricted data URL");
    const auto reject = [](Bytes value, Error code, const char* why) {
        try { ValidateAvatar(std::move(value)); } catch (const Failure& error) { Expect(error.code == code, why); return; }
        Expect(false, why);
    };
    reject({}, Error::InvalidAvatar, "empty image");
    reject(Bytes(MaxAvatarBytes + 1, 0), Error::AvatarTooLarge, "2MiB byte cap");
    reject(Bytes{'<','s','v','g','>'}, Error::InvalidAvatar, "no SVG");
    auto truncated = png; truncated.resize(24);
    reject(truncated, Error::InvalidAvatar, "magic bytes alone insufficient");
    auto bomb = png; bomb[16] = 0x7f; bomb[20] = 0x7f;
    reject(bomb, Error::InvalidAvatar, "pixel allocation bounded");
    auto wrong = image; wrong.mime = "text/html";
    bool safe = false;
    try { AvatarDataUrl(wrong); } catch (const Failure&) { safe = true; }
    Expect(safe, "cannot label arbitrary data as image");
    std::cout << "native avatar byte validation passed\n";
}
