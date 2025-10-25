#include "menu.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <nativefiledialog/nfd.h>
DISABLE_WARNINGS_POP()
#include <filesystem>
#include <iostream>

Menu::Menu(Config& config)
: m_config(config)
, m_newParticleCount(config.numParticles)
{}

void Menu::draw() {
    ImGui::Begin("Debug Controls");
    
    ImGui::Text("Particle Simulation");
    ImGui::Separator();
    drawParticleSimControls();
    ImGui::Spacing();
    ImGui::Text("Sphere Container");
    ImGui::Separator();
    drawSphereContainerControls();
    ImGui::Spacing();
    ImGui::Text("Particle Coloring");
    ImGui::Separator();
    drawParticleColorControls();

    ImGui::End();
}

void Menu::drawParticleSimControls() {
    // Parameters
    m_newParticleCount = std::max(1, m_newParticleCount); // Ensure that the new number of particles is always positive
    ImGui::InputInt("New particle count", &m_newParticleCount);
    ImGui::SliderFloat("Timestep", &m_config.particleSimTimestep, 0.001f, 0.05f, "%.3f");
    ImGui::SliderFloat("Particle radius", &m_config.particleRadius, 0.05f, 1.0f);
    ImGui::Checkbox("Inter-particle collisions", &m_config.particleInterCollision);

    // Flags
    std::string simPlaybackText = m_config.doContinuousSimulation ? "Pause simulation" : "Resume simulation";
    if (ImGui::Button(simPlaybackText.c_str())) { m_config.doContinuousSimulation = !m_config.doContinuousSimulation; }
    ImGui::SameLine();
    m_config.doSingleStep = ImGui::Button("Single simulation step");
    ImGui::SameLine();
    if (ImGui::Button("Reset simulation")) {
        m_config.numParticles       = m_newParticleCount;
        m_config.doResetSimulation  = true;
    }
}

void Menu::drawSphereContainerControls() {
    constexpr float CENTER_MAX              = 10.0f;
    constexpr float RADIUS_MAX              = 10.0f;
    constexpr float WIREFRAME_THICKNESS_MAX = 10.0f;

    ImGui::DragFloat3("Center", glm::value_ptr(m_config.sphereCenter), 0.01f, -CENTER_MAX, CENTER_MAX, "%.2f");
    ImGui::DragFloat("Radius", &m_config.sphereRadius, 0.01f, 0.0f, RADIUS_MAX, "%.2f");
    ImGui::ColorEdit3("Color", glm::value_ptr(m_config.sphereColor));
}

void Menu::drawParticleColorControls() {
	ImGui::Checkbox("Shading", &m_config.shading);
	ImGui::Checkbox("Speed based coloring", &m_config.speedBasedColoring);

	if (m_config.speedBasedColoring) {
		ImGui::SliderFloat("Min speed", &m_config.minSpeed, 0.0f, m_config.maxSpeed, "%.2f");
		ImGui::SliderFloat("Max speed", &m_config.maxSpeed, m_config.minSpeed, 20.0f, "%.2f");

		ImGui::ColorEdit3("Min speed color", glm::value_ptr(m_config.minSpeedColor));
		ImGui::ColorEdit3("Max speed color", glm::value_ptr(m_config.maxSpeedColor));
	} else {
		ImGui::ColorEdit3("Particle color", glm::value_ptr(m_config.baseParticleColor));
	}

	ImGui::SliderFloat("Ambient light intensity", &m_config.ambient, 0.0f, 1.0f, "%.2f");

	ImGui::Separator();
	ImGui::Text("Bounce coloring");

	ImGui::Checkbox("Enable bounce coloring", &m_config.enableBounceColoring);
	ImGui::ColorEdit3("Bounce color", glm::value_ptr(m_config.bounceColor));
	ImGui::SliderInt("Blink frames duration", &m_config.blinkDurationFrames, 60, 600);
	ImGui::SliderInt("Blink speed", &m_config.blinkSpeedFrames, 15, m_config.blinkDurationFrames);
	ImGui::SliderInt("Bounce threshold", &m_config.bounceThreshold, 1, 50);
}

