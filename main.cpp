#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "read_chunk.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cmath>
#include <thread>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Cube Volleyball";
		glm::uvec2 size = glm::uvec2(640, 480);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	//SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_Normal = 0;
	GLuint program_Color = 0;
	GLuint program_mvp = 0;
	GLuint program_itmv = 0;
	GLuint program_to_light = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"uniform mat3 itmv;\n"
			"in vec4 Position;\n"
			"in vec3 Normal;\n"
			"in vec3 Color;\n"
			"out vec3 normal;\n"
			"out vec3 color;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	normal = itmv * Normal;\n"
			"   color = Color;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform vec3 to_light;\n"
			"in vec3 normal;\n"
			"in vec3 color;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"   vec3 l = mix(normal,to_light,0.9);\n"
			"	float light = max(0.0, dot(normalize(normal), l));\n"
			"	fragColor = vec4(light * color, 1.0);\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_Normal = glGetAttribLocation(program, "Normal");
		if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");
		program_Color = glGetAttribLocation(program, "Color");
		if (program_Color == -1U) throw std::runtime_error("no attribute named Color");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_itmv = glGetUniformLocation(program, "itmv");
		if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

		program_to_light = glGetUniformLocation(program, "to_light");
		if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
	}

	//------------ meshes ------------

	Meshes meshes;

	{ //add meshes to database:
		Meshes::Attributes attributes;
		attributes.Position = program_Position;
		attributes.Normal = program_Normal;
		attributes.Color = program_Color;

		meshes.load("meshes.blob", attributes);
	}

	//------------ scene ------------

	Scene scene;
	//set up camera parameters based on window:
	scene.camera.fovy = glm::radians(60.0f);
	scene.camera.aspect = float(config.size.x) / float(config.size.y);
	scene.camera.near_plane = 0.01f;
	//(transform will be handled in the update function below)

	//add some objects from the mesh library:
	auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) -> Scene::Object & {
		Mesh const &mesh = meshes.get(name);
		Scene::Object object;
		object.transform.position = position;
		object.transform.rotation = rotation;
		object.transform.scale = scale;
		object.vao = mesh.vao;
		object.start = mesh.start;
		object.count = mesh.count;
		object.program = program;
		object.program_mvp = program_mvp;
		object.program_itmv = program_itmv;
		scene.objects[name]=object;
		return scene.objects[name];
	};


	{ //read objects to add from "scene.blob":
		std::ifstream file("scene.blob", std::ios::binary);

		std::vector< char > strings;
		//read strings chunk:
		read_chunk(file, "str0", &strings);

		{ //read scene chunk, add meshes to scene:
			struct SceneEntry {
				uint32_t name_begin, name_end;
				glm::vec3 position;
				glm::quat rotation;
				glm::vec3 scale;
			};
			static_assert(sizeof(SceneEntry) == 48, "Scene entry should be packed");

			std::vector< SceneEntry > data;
			read_chunk(file, "scn0", &data);

			for (auto const &entry : data) {
				if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
					throw std::runtime_error("index entry has out-of-range name begin/end");
				}
				std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
				add_object(name, entry.position, entry.rotation, entry.scale);
			}
		}
	}

	//add_object("Link3", glm::vec3(0.0f, 0.0f, 1.0f), glm::quat(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(1.0f));

	

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

	struct {
		float radius = 18.0f;
		float elevation = -10.0f;
		float azimuth = 0.0f;
		glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
	} camera;

	//------------ game loop ------------

	float p1y = 0.0f, p1z = 0.0f, p2y = 0.0f, p2z = 0.0f;
	float by = 0.0f, bz = 0.0f;
	int hits = 0;
	int lastHit = 1;
	auto player1 = &scene.objects["Cube"];
	auto player2 = &scene.objects["Cube.001"];
	auto ball = &scene.objects["Sphere"];
	ball->transform.position.z = 7.5f;

	bool should_quit = false;
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
			}
			else if (evt.type == SDL_MOUSEBUTTONDOWN) {
			}
			else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			}
			else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_w && p1z == 0.0f && player1->transform.position.z == 0.5f) {
				p1z = 10.0f;
			}
			else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_UP && p2z == 0.0f && player2->transform.position.z == 0.5f) {
				p2z = 10.0f;
			}
			else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			}
		}
		if (should_quit) break;

		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
		previous_time = current_time;

		{ //update game state:
			auto state = SDL_GetKeyboardState(nullptr);
			if (state[SDL_SCANCODE_A] && !state[SDL_SCANCODE_D])
				p1y = 5.0f;
			else if (!state[SDL_SCANCODE_A] && state[SDL_SCANCODE_D])
				p1y = -5.0f;
			else
				p1y = 0.0f;
			if (state[SDL_SCANCODE_LEFT] && !state[SDL_SCANCODE_RIGHT])
				p2y = 5.0f;
			else if (!state[SDL_SCANCODE_LEFT] && state[SDL_SCANCODE_RIGHT])
				p2y = -5.0f;
			else
				p2y = 0.0f;

			//Player and ball collision
			if ((abs(ball->transform.position.z - player1->transform.position.z) <= 1.0f &&
					abs(ball->transform.position.y - player1->transform.position.y) <= 0.5f) ||
				(abs(ball->transform.position.z - player1->transform.position.z) <= 0.5f &&
					abs(ball->transform.position.y - player1->transform.position.y) <= 1.0f) ||
				std::pow(ball->transform.position.y- player1->transform.position.y + 0.5f,2.0f) + 
					std::pow(ball->transform.position.z - player1->transform.position.z + 0.5f, 2.0f) <= 0.25 ||
				std::pow(ball->transform.position.y - player1->transform.position.y - 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player1->transform.position.z + 0.5f, 2.0f) <= 0.25 || 
				std::pow(ball->transform.position.y - player1->transform.position.y + 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player1->transform.position.z - 0.5f, 2.0f) <= 0.25 || 
				std::pow(ball->transform.position.y - player1->transform.position.y - 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player1->transform.position.z - 0.5f, 2.0f) <= 0.25){
				if (lastHit != 1) {
					lastHit = 1;
					hits = 0;
				}
				if(bz < 0.0f)
					hits++;
				by += (ball->transform.position.y - player1->transform.position.y) + p1y;
				float temp = bz;
				bz = p1z + 2.0f;
				if (p1z != 0.0f)
					p1z = temp;
			}
			else if ((abs(ball->transform.position.z - player2->transform.position.z) <= 1.0f &&
					abs(ball->transform.position.y - player2->transform.position.y) <= 0.5f) ||
				(abs(ball->transform.position.z - player2->transform.position.z) <= 0.5f &&
					abs(ball->transform.position.y - player2->transform.position.y) <= 1.0f) ||
				std::pow(ball->transform.position.y - player2->transform.position.y + 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player2->transform.position.z + 0.5f, 2.0f) <= 0.25 ||
				std::pow(ball->transform.position.y - player2->transform.position.y - 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player2->transform.position.z + 0.5f, 2.0f) <= 0.25 ||
				std::pow(ball->transform.position.y - player2->transform.position.y + 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player2->transform.position.z - 0.5f, 2.0f) <= 0.25 ||
				std::pow(ball->transform.position.y - player2->transform.position.y - 0.5f, 2.0f) +
					std::pow(ball->transform.position.z - player2->transform.position.z - 0.5f, 2.0f) <= 0.25) {
				if (lastHit != 2) {
					lastHit = 2;
					hits = 0;
				}
				if (bz < 0.0f)
					hits++;
				by += (ball->transform.position.y - player2->transform.position.y) + p2y;
				float temp = bz;
				bz = p2z + 2.0f;
				if (p2z != 0.0f)
					p2z = temp;

			}
			else if (ball->transform.position.z != 0.5f) {
				bz = bz - 10.0f*elapsed;
			}
			by = by * std::pow(0.9f,elapsed);
			if (player2->transform.position.z != 0.5f)
				p2z = p2z - 10.0f*elapsed;
			if (player1->transform.position.z != 0.5f)
				p1z = p1z - 10.0f*elapsed;

			//Translations
			player1->transform.position.y += elapsed * p1y;
			player1->transform.position.z += elapsed * p1z;
			player2->transform.position.y += elapsed * p2y;
			player2->transform.position.z += elapsed * p2z;
			ball->transform.position.y += elapsed * by;
			ball->transform.position.z += elapsed * bz;

			//Player and world collision
			if (player1->transform.position.z <= 0.5f) {
				p1z = 0.0f;
				player1->transform.position.z = 0.5f;
			}
			if (player1->transform.position.y > 9.5f) {
				p1y = 0.0f;
				player1->transform.position.y = 9.5f;
			} else if(player1->transform.position.y < 1.0f) {
				p1y = 0.0f;
				player1->transform.position.y = 1.0f;
			}
			if (player2->transform.position.z <= 0.5f) {
				p2z = 0.0f;
				player2->transform.position.z = 0.5f;
			}
			if (player2->transform.position.y > -1.0f) {
				p2y = 0.0f;
				player2->transform.position.y = -1.0f;
			}
			else if (player2->transform.position.y < -9.5f) {
				p2y = 0.0f;
				player2->transform.position.y = -9.5f;
			}

			//Point condition
			if (ball->transform.position.z <= 0.5f || hits >= 4 ||
				(std::abs(ball->transform.position.y) <= 0.5f && ball->transform.position.z <= 3.5f) ||
				ball->transform.position.y >= 9.5f || ball->transform.position.y <= -9.5){
				bz = 0.0f;
				by = 0.0f;
				player1->transform.position = glm::vec3(0.0f, 5.0f, 0.5f);
				player2->transform.position = glm::vec3(0.0f, -5.0f, 0.5f);
				//Net
				if (std::abs(ball->transform.position.y) <= 0.5f && ball->transform.position.z <= 3.5f) {
					if (lastHit == 1) {
						lastHit = 2;
						ball->transform.position = glm::vec3(0.0f, -5.0f, 7.5f);
					}
					else {
						ball->transform.position = glm::vec3(0.0f, 5.0f, 7.5f);
						lastHit = 1;
					}
				}
				//Ball dropped
				else if (ball->transform.position.y < 0.0f && ball->transform.position.y > -9.5) {
					ball->transform.position = glm::vec3(0.0f, 5.0f, 7.5f);
					lastHit = 1;
				}
				else if (ball->transform.position.y > 0.0f && ball->transform.position.y < 9.5){
					ball->transform.position = glm::vec3(0.0f, -5.0f, 7.5f);
					lastHit = 2;
				}
				//Out of bounds
				else {
					if (lastHit == 1) {
						lastHit = 2;
						ball->transform.position = glm::vec3(0.0f, -5.0f, 7.5f);
					}
					else {
						ball->transform.position = glm::vec3(0.0f, 5.0f, 7.5f);
						lastHit = 1;
					}
				}
				hits = 0;
			}
			
			//camera:
			scene.camera.transform.position = camera.radius * glm::vec3(
				std::cos(camera.elevation) * std::cos(camera.azimuth),
				std::cos(camera.elevation) * std::sin(camera.azimuth),
				std::sin(camera.elevation)) + camera.target;

			glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
			glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
			up = glm::normalize(up - glm::dot(up, out) * out);
			glm::vec3 right = glm::cross(up, out);

			scene.camera.transform.rotation = glm::quat_cast(
				glm::mat3(right, up, out)
			);
			scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
		}

		//draw output:
		glClearColor(0.5, 0.5, 0.5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			glUseProgram(program);
			glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(glm::vec3(0.0f, 1.0f, 10.0f))));
			scene.render();
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}