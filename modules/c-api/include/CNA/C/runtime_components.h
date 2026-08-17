// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_RUNTIME_COMPONENTS_H
#define CNA_C_RUNTIME_COMPONENTS_H

#include "CNA/C/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle to one game component. */
typedef CNA_Handle CNA_GameComponentHandle;

/** @brief Owned handle to one component event subscription. */
typedef CNA_Handle CNA_GameComponentEventRegistrationHandle;

/** @brief Fixed-width identity of a component event. */
typedef uint32_t CNA_GameComponentEvent;

/** @brief The component's enabled state changed. */
#define CNA_GAME_COMPONENT_EVENT_ENABLED_CHANGED UINT32_C(0)
/** @brief The component's update order changed. */
#define CNA_GAME_COMPONENT_EVENT_UPDATE_ORDER_CHANGED UINT32_C(1)
/** @brief The component's draw order changed; drawable components only. */
#define CNA_GAME_COMPONENT_EVENT_DRAW_ORDER_CHANGED UINT32_C(2)
/** @brief The component's visibility changed; drawable components only. */
#define CNA_GAME_COMPONENT_EVENT_VISIBLE_CHANGED UINT32_C(3)
/** @brief The component was disposed. */
#define CNA_GAME_COMPONENT_EVENT_DISPOSED UINT32_C(4)
/** @brief Highest defined component-event identity. */
#define CNA_GAME_COMPONENT_EVENT_MAXIMUM CNA_GAME_COMPONENT_EVENT_DISPOSED

/**
 * @brief Handler invoked for a component event that carries no data.
 *
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_GameComponentEventCallback)(void* context);

/**
 * @brief The behavior of one component a C consumer supplies.
 *
 * A component is the one place in this ABI where a caller does not consume canonical behavior but
 * **provides** it: the canonical types are C++ interfaces, and C cannot implement an interface, so a
 * component is this callback set plus a context and the ABI supplies the object that implements the
 * interfaces and forwards to it. Every member may be null, and a null member is simply not called —
 * that is how a component opts out of a lifecycle step rather than by implementing an empty one.
 *
 * The handlers run on the game thread, inside the frame that triggers them, and a handler that
 * fails has nowhere to report it: return normally and record the failure in your own context.
 */
typedef struct CNA_GameComponentCallbacks {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Invoked once, when the game initializes the component. */
    void (*initialize)(void* context);

    /** @brief Invoked once per frame while the component is enabled. */
    void (*update)(const CNA_GameTime* game_time, void* context);

    /** @brief Invoked once per frame while a drawable component is visible. */
    void (*draw)(const CNA_GameTime* game_time, void* context);

    /** @brief Invoked when a drawable component should load its graphics content. */
    void (*load_content)(void* context);

    /** @brief Invoked when a drawable component should release its graphics content. */
    void (*unload_content)(void* context);

    /** @brief Invoked when the component is disposed, before its handle is released. */
    void (*dispose)(void* context);

    /** @brief Caller context passed back to every handler above. */
    void* context;
} CNA_GameComponentCallbacks;

/**
 * @brief Initializes a component callback set with no handlers.
 *
 * @param out_callbacks Receives a versioned structure whose handlers and context are all null.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_game_component_callbacks_init(CNA_GameComponentCallbacks* out_callbacks);

/**
 * @brief Creates an updateable component.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param callbacks Behavior the component forwards to; copied during this call.
 * @param out_component Receives an owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Creating a component does **not** add it to the game: it exists on its own until
 * `cna_game_components_add` puts it in the collection the game drives, exactly as canonically. The
 * drawing and content handlers are ignored by this kind of component; use
 * `cna_drawable_game_component_create` for one that draws.
 */
CNA_C_API CNA_Result cna_game_component_create(
    CNA_Handle game,
    const CNA_GameComponentCallbacks* callbacks,
    CNA_GameComponentHandle* out_component);

/**
 * @brief Creates a drawable component.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param callbacks Behavior the component forwards to; copied during this call.
 * @param out_component Receives an owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A drawable component is an updateable one that also draws, so every route below accepts either
 * kind and the draw-order and visibility routes are the ones that need this kind.
 */
CNA_C_API CNA_Result cna_drawable_game_component_create(
    CNA_Handle game,
    const CNA_GameComponentCallbacks* callbacks,
    CNA_GameComponentHandle* out_component);

/**
 * @brief Reports whether a component draws.
 *
 * @param component Owned component handle.
 * @param out_drawable Receives `CNA_TRUE` for a drawable component.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_component_get_is_drawable(
    CNA_GameComponentHandle component,
    CNA_Bool* out_drawable);

/**
 * @brief Returns the game a component belongs to.
 *
 * @param component Owned component handle.
 * @param out_game Receives the game handle the component was created from.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A component belongs to exactly one game for its whole life; the canonical property has no setter
 * and neither does this.
 */
CNA_C_API CNA_Result cna_game_component_get_game(
    CNA_GameComponentHandle component,
    CNA_Handle* out_game);

/**
 * @brief Reports whether a component is updated each frame.
 *
 * @param component Owned component handle.
 * @param out_enabled Receives `CNA_TRUE` when the component is enabled.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_component_get_enabled(
    CNA_GameComponentHandle component,
    CNA_Bool* out_enabled);

/**
 * @brief Enables or disables a component.
 *
 * @param component Owned component handle.
 * @param enabled Whether the component is updated each frame.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Changing the value raises the enabled-changed event; setting it to what it already is does not.
 */
CNA_C_API CNA_Result cna_game_component_set_enabled(
    CNA_GameComponentHandle component,
    CNA_Bool enabled);

/**
 * @brief Returns a component's update order.
 *
 * @param component Owned component handle.
 * @param out_order Receives the order value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_component_get_update_order(
    CNA_GameComponentHandle component,
    int32_t* out_order);

/**
 * @brief Sets a component's update order.
 *
 * @param component Owned component handle.
 * @param order New order value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Changing the value raises the update-order-changed event; setting it to what it already is does
 * not.
 */
CNA_C_API CNA_Result cna_game_component_set_update_order(
    CNA_GameComponentHandle component,
    int32_t order);

/**
 * @brief Returns a drawable component's draw order.
 *
 * @param component Owned component handle.
 * @param out_order Receives the order value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_drawable_game_component_get_draw_order(
    CNA_GameComponentHandle component,
    int32_t* out_order);

/**
 * @brief Sets a drawable component's draw order.
 *
 * @param component Owned component handle.
 * @param order New order value.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_drawable_game_component_set_draw_order(
    CNA_GameComponentHandle component,
    int32_t order);

/**
 * @brief Reports whether a drawable component is drawn.
 *
 * @param component Owned component handle.
 * @param out_visible Receives `CNA_TRUE` when the component is visible.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_drawable_game_component_get_visible(
    CNA_GameComponentHandle component,
    CNA_Bool* out_visible);

/**
 * @brief Shows or hides a drawable component.
 *
 * @param component Owned component handle.
 * @param visible Whether the component is drawn.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_drawable_game_component_set_visible(
    CNA_GameComponentHandle component,
    CNA_Bool visible);

/**
 * @brief Borrows the graphics device a drawable component draws with.
 *
 * @param component Owned component handle.
 * @param out_graphics_device Receives the callback-scoped borrowed device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw or a
 *         game with no device, or a documented argument/handle/thread failure.
 *
 * The device is the game's own, borrowed on the same terms as `cna_game_get_graphics_device`.
 */
CNA_C_API CNA_Result cna_drawable_game_component_get_graphics_device(
    CNA_GameComponentHandle component,
    CNA_Handle* out_graphics_device);

/**
 * @brief Orders two components the way the canonical comparison does.
 *
 * @param component Owned component handle.
 * @param other Owned component handle to compare against.
 * @param out_order Receives a negative value, zero or a positive value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical comparison orders by update order alone, and it subtracts **this** component's order
 * from the other's rather than the other way round, so a component that updates earlier compares
 * greater. That inversion is preserved rather than corrected. Two components with the same update
 * order compare equal even though they are different objects.
 */
CNA_C_API CNA_Result cna_game_component_compare_to(
    CNA_GameComponentHandle component,
    CNA_GameComponentHandle other,
    int32_t* out_order);

/**
 * @brief Runs a component's initialization once.
 *
 * @param component Owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The game initializes every component it owns, so a caller normally never calls this; it is here
 * because the canonical operation is public.
 */
CNA_C_API CNA_Result cna_game_component_initialize(CNA_GameComponentHandle component);

/**
 * @brief Updates a component once.
 *
 * @param component Owned component handle.
 * @param game_time Elapsed and total time for this update.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The game updates every enabled component it owns; this route exists because the canonical
 * operation is public, and it does **not** consult the enabled flag, exactly as canonically.
 */
CNA_C_API CNA_Result cna_game_component_update(
    CNA_GameComponentHandle component,
    const CNA_GameTime* game_time);

/**
 * @brief Draws a drawable component once.
 *
 * @param component Owned component handle.
 * @param game_time Elapsed and total time for this frame.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` for a component that does not draw, or a
 *         documented argument/handle/thread/native failure.
 *
 * As with the update route, this does not consult the visibility flag.
 */
CNA_C_API CNA_Result cna_drawable_game_component_draw(
    CNA_GameComponentHandle component,
    const CNA_GameTime* game_time);

/**
 * @brief Disposes a component without releasing its handle.
 *
 * @param component Owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Disposal raises the disposed event once. Unlike a sensor, a second disposal is a no-op rather than
 * a refusal, which is this canonical type's own behavior.
 */
CNA_C_API CNA_Result cna_game_component_dispose(CNA_GameComponentHandle component);

/**
 * @brief Returns the byte count of a component's .NET type name.
 *
 * @param component Owned component handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_component_get_type_name_size(
    CNA_GameComponentHandle component,
    uint64_t* out_bytes);

/**
 * @brief Copies a component's fully-qualified .NET type name.
 *
 * @param component Owned component handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 *
 * The name is the canonical component type's, not the caller's: this ABI's components are ordinary
 * `GameComponent` and `DrawableGameComponent` objects that forward to C.
 */
CNA_C_API CNA_Result cna_game_component_copy_type_name(
    CNA_GameComponentHandle component,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to one of a component's events.
 *
 * @param component Owned component handle.
 * @param event One `CNA_GAME_COMPONENT_EVENT_*` identity.
 * @param callback Handler invoked when the event is raised.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity,
 *         `CNA_RESULT_INVALID_STATE` for a drawing event on a component that does not draw, or a
 *         documented handle/thread failure.
 *
 * Every canonical component event carries nothing but its sender, so the handler receives only its
 * context. The registration keeps the component alive, so a subscription can never outlive it.
 */
CNA_C_API CNA_Result cna_game_component_subscribe(
    CNA_GameComponentHandle component,
    CNA_GameComponentEvent event,
    CNA_GameComponentEventCallback callback,
    void* context,
    CNA_GameComponentEventRegistrationHandle* out_registration);

/**
 * @brief Releases a component event registration.
 *
 * @param registration Owned registration handle from any component subscribe route.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 */
CNA_C_API CNA_Result cna_game_component_unsubscribe(
    CNA_GameComponentEventRegistrationHandle registration);

/**
 * @brief Releases a component handle.
 *
 * @param component Owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Releasing removes the component from the game's collection first if it is still there, which the
 * canonical destructor does not do — a C++ caller manages that itself, while a handle-based ABI must
 * not leave the runtime holding a pointer to something it has released. Every component must be
 * released before its game is destroyed.
 */
CNA_C_API CNA_Result cna_game_component_destroy(CNA_GameComponentHandle component);

/**
 * @brief Handler invoked when the game's component collection changes.
 *
 * @param component The component that was added or removed.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_GameComponentCollectionCallback)(
    CNA_GameComponentHandle component,
    void* context);

/**
 * @brief Returns how many components the game holds.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the component count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A game owns exactly one component collection, so the collection needs no handle of its own and
 * every route here addresses the game's.
 */
CNA_C_API CNA_Result cna_game_components_get_count(CNA_Handle game, uint64_t* out_count);

/**
 * @brief Returns the component at one position in the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the reported count.
 * @param out_component Receives the component handle, or `CNA_INVALID_HANDLE` for a component this
 *        ABI did not create.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index at or past the count, or
 *         a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_components_get_at(
    CNA_Handle game,
    uint64_t index,
    CNA_GameComponentHandle* out_component);

/**
 * @brief Adds a component to the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param component Owned component handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical collection accepts the same component twice, and this route does too rather than
 * inventing a refusal. A component added after the game has initialized is initialized when it is
 * added.
 */
CNA_C_API CNA_Result cna_game_components_add(CNA_Handle game, CNA_GameComponentHandle component);

/**
 * @brief Inserts a component at one position in the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based position, at most the current count.
 * @param component Owned component handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index past the count, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_game_components_insert(
    CNA_Handle game,
    uint64_t index,
    CNA_GameComponentHandle component);

/**
 * @brief Removes a component from the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param component Owned component handle.
 * @param out_removed Receives `CNA_TRUE` when the component was in the collection.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Removing a component does not dispose or release it: the handle stays valid and can be added
 * again.
 */
CNA_C_API CNA_Result cna_game_components_remove(
    CNA_Handle game,
    CNA_GameComponentHandle component,
    CNA_Bool* out_removed);

/**
 * @brief Removes the component at one position in the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index at or past the count, or
 *         a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_game_components_remove_at(CNA_Handle game, uint64_t index);

/**
 * @brief Removes every component from the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Every removal raises the component-removed event, one per component.
 */
CNA_C_API CNA_Result cna_game_components_clear(CNA_Handle game);

/**
 * @brief Reports whether the game's collection holds a component.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param component Owned component handle.
 * @param out_contains Receives `CNA_TRUE` when the component is in the collection.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_components_contains(
    CNA_Handle game,
    CNA_GameComponentHandle component,
    CNA_Bool* out_contains);

/**
 * @brief Returns a component's position in the game's collection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param component Owned component handle.
 * @param out_index Receives the zero-based position, or **-1** when the component is not in the
 *        collection.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical -1 for a component that is not present is preserved rather than replaced by a
 * separate presence flag.
 */
CNA_C_API CNA_Result cna_game_components_index_of(
    CNA_Handle game,
    CNA_GameComponentHandle component,
    int32_t* out_index);

/**
 * @brief Subscribes to the game's component-added event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param callback Handler invoked with each added component.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical event argument carries the component and nothing else, so the handler receives the
 * component handle directly rather than a description of one.
 */
CNA_C_API CNA_Result cna_game_components_subscribe_added(
    CNA_Handle game,
    CNA_GameComponentCollectionCallback callback,
    void* context,
    CNA_GameComponentEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the game's component-removed event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param callback Handler invoked with each removed component.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_components_subscribe_removed(
    CNA_Handle game,
    CNA_GameComponentCollectionCallback callback,
    void* context,
    CNA_GameComponentEventRegistrationHandle* out_registration);

/**
 * @brief Returns the byte count of the component collection's .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_components_get_type_name_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the component collection's fully-qualified .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_components_copy_type_name(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Fixed-width identity of a service the runtime registers with a game. */
typedef uint32_t CNA_GameServiceType;

/** @brief The graphics device manager service. */
#define CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER UINT32_C(0)
/** @brief The graphics device service. */
#define CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE UINT32_C(1)
/** @brief Highest defined game-service identity. */
#define CNA_GAME_SERVICE_TYPE_MAXIMUM CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE

/**
 * @brief Reports whether one canonical service is registered with the game.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param service One `CNA_GAME_SERVICE_TYPE_*` identity.
 * @param out_present Receives `CNA_TRUE` when the service is registered.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 *
 * **This covers the services the runtime registers, not every service the canonical container can
 * hold.** The canonical container is keyed by C++ type identity, which has no C expression: a C
 * consumer cannot name a type, and cannot author an object implementing a C++ interface to register
 * under one. So the lookup is by named identity, and a caller's own services belong in the context
 * pointer every callback in this ABI already carries.
 */
CNA_C_API CNA_Result cna_game_services_contains_ext(
    CNA_Handle game,
    CNA_GameServiceType service,
    CNA_Bool* out_present);

/**
 * @brief Removes one canonical service from the game.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param service One `CNA_GAME_SERVICE_TYPE_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity, or a
 *         documented handle/thread failure.
 *
 * Removing a service the runtime registered is permitted because the canonical operation permits it,
 * and it cannot be undone from C: nothing here can register a service. The game keeps working off
 * the pointers it resolved while initializing, so what a removal changes is what a **later** lookup
 * finds. Removing a service that is not registered is an ordinary success.
 */
CNA_C_API CNA_Result cna_game_services_remove_ext(CNA_Handle game, CNA_GameServiceType service);

#ifdef __cplusplus
}
#endif

#endif
