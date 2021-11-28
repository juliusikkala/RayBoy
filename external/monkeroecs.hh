/*
The MIT License (MIT)

Copyright (c) 2020, 2021 Julius Ikkala

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/** \mainpage MonkeroECS
 *
 * \section intro_sec Introduction
 *
 * MonkeroECS is a fairly small, header-only Entity Component System rescued
 * from a game engine. It aims for simple and terse usage and also contains an
 * event system.
 *
 * MonkeroECS is written in C++17 and the only dependency is the standard
 * library. Note that the code isn't pretty and has to do some pretty gnarly
 * trickery to make the usable as terse as it is.
 *
 * While performance is one of the goals of this ECS, it was more written with
 * flexibility in mind. It can iterate over large numbers of entities quickly
 * (this case was optimized for), but one-off lookups of individual components
 * is logarithmic. On the flipside, there is no maximum limit to the number of
 * entities other than the entity ID being 32-bit (just change it to 64-bit if
 * you are a madman and need more, it should work but consumes more memory).
 *
 * Adding the ECS to your project is simple; just copy the monkeroecs.hh yo
 * your codebase and include it!
 *
 * The name is a reference to the game the ECS was originally created for, but
 * it was unfortunately never finished or released despite the engine being in a
 * finished state.
 */
#ifndef MONKERO_ECS_HH
#define MONKERO_ECS_HH
#include <functional>
#include <unordered_set>
#include <algorithm>
#include <vector>
#include <memory>
#include <tuple>

/** This namespace contains all of MonkeroECS. */
namespace monkero
{

/** The entity type, it's just an ID.
 * An entity alone will not take up memory in the ECS, only once components are
 * added does the entity truly use memory. You can change this to uint64_t if
 * you truly need over 4 billion entities and have tons of memory.
 */
using entity = uint32_t;

/** A base class for components which need to have unchanging memory addresses.
 * Derive from this if you want to have an extra layer of indirection in the
 * storage of this component. What this allows is no move or copy constructor
 * calls and faster add & remove at the cost of iteration speed. Additionally,
 * the pointer to the component will not become outdated until that specific
 * component is removed.
 */
struct ptr_component {};
class ecs;

/** All systems must derive from this class.
 * This pretty much solely exists to force a common vtable basis which is needed
 * due to some dynamic_cast trickery.
 */
class system
{
public:
    /** An empty destructor that creates the vtable. */
    virtual ~system() = default;
};

/** A built-in event emitted when a component is added to the ECS. */
template<typename Component>
struct add_component
{
    entity id; /**< The entity that got the component */
    Component* data; /**< A pointer to the component */
};

/** A built-in event emitted when a component is removed from the ECS.
 * The destructor of class ecs will emit remove_component events for all
 * components still left at that point.
 */
template<typename Component>
struct remove_component
{
    entity id; /**< The entity that lost the component */
    Component* data; /**< A pointer to the component (it's not destroyed yet) */
};

template<typename EventType>
class event_emitter;

// Provides event receiving facilities for one type alone. Derive
// from class receiver instead in your own code.
template<typename EventType>
class event_receiver
{
friend class event_emitter<EventType>;
public:
    event_receiver();
    event_receiver(const event_receiver& other) = delete;
    event_receiver(event_receiver&& other) = delete;
    virtual ~event_receiver();

    /** Receivers must implement this for each received event type.
     * It's called by emitters when an event of EventType is emitted.
     * \param ctx The ECS this receiver is part of.
     * \param event The event that occurred.
     */
    virtual void handle(ecs& ctx, const EventType& event) = 0;

private:
    // This could be a vector, but iteration isn't time-critical here so I went
    // the easy way.
    std::unordered_set<event_emitter<EventType>*> subscriptions;
};

// Same as event_receiver, don't derive from this directly. Derive from class
// emitter instead.
template<typename EventType>
class event_emitter
{
friend class event_receiver<EventType>;
public:
    event_emitter();
    event_emitter(const event_emitter& other) = delete;
    event_emitter(event_emitter&& other) = delete;
    virtual ~event_emitter();

    void emit(ecs& ctx, const EventType& event);
    size_t subscriber_count() const;
    void subscribe(event_receiver<EventType>& s);

private:
    std::vector<event_receiver<EventType>*> subscribers;
};

/** Deriving from this class allows systems to receive events of the specified
 * type(s). Just give a list of the desired event types in the template
 * parameters and the system will start receiving them from all other systems
 * automatically.
 */
template<typename... ReceiveEvents>
class receiver: public event_receiver<ReceiveEvents>...
{
friend class ecs;
public:
    // Only for internal use, it's only public because of an implementation
    // difficulty.
    void connect_all_receivers(system& emitter);
private:
    template<typename Event>
    void try_connect(system& emitter);
};

/** Deriving from this class allows systems to emit events of the specified
 * type(s).
 */
template<typename... EmitEvents>
class emitter: public event_emitter<EmitEvents>...
{
public:
    // For whatever reason, C++ can't disambiguate between the different
    // inherited emit() functions _despite_ the obvious argument type
    // difference. This template fixes that behaviour.
    /** Sends the given event to all receivers waiting for an event of this
     * type.
     * \param ctx The ECS of the system that is calling this function.
     * \param event The event that you want to emit.
     */
    template<typename EventType>
    void emit(ecs& ctx, const EventType& event);
};

/** The primary class of the ECS.
 * Entities are created by it, components are attached throught it and
 * systems are created in it.
 */
class ecs: public system
{
public:
    /** The constructor. */
    inline ecs();
    /** The destructor.
     * It ensures that all remove_component events are sent for the remainder
     * of the components before systems are cleared.
     */
    inline ~ecs();

    /** Calls a given function for all suitable entities.
     * The parameters of the function mandate how it is called. Batching is
     * enabled automatically so that removing and adding entities and components
     * during iteration is safe.
     * \param f The iteration callback.
     *   The first parameter of \p f must be #entity (the entity that the
     *   iteration concerns.) After that, further parameters must be either
     *   references or pointers to components. The function is only called when
     *   all referenced components are present for the entity; pointer
     *   parameters are optional and can be null if the component is not
     *   present.
     */
    template<typename F>
    inline void foreach(F&& f);

    /** Same as foreach(), just syntactic sugar.
     * \see foreach()
     */
    template<typename F>
    inline void operator()(F&& f);

    /** Reserves space for components.
     * Using this function isn't mandatory, but it can be used to improve
     * performance when about to add a known number of components.
     * \tparam Component The type of component to reserve memory for.
     * \param count The number of components to reserve memory for.
     */
    template<typename Component>
    void reserve(size_t count);

    /** Adds an entity without components.
     * No memory is reserved, as the operation literally just increments a
     * counter.
     * \return The new entity ID.
     */
    inline entity add();

    /** Adds an entity with initial components.
     * Takes a list of component instances. Note that they must be moved in, so
     * you should create them during the call or use std::move().
     * \param components All components that should be included.
     * \return The new entity ID.
     */
    template<typename... Components>
    entity add(Components&&... components);

    /** Adds components to an existing entity.
     * Takes a list of component instances. Note that they must be moved in, so
     * you should create them during the call or use std::move().
     * \param id The entity that components are added to.
     * \param components All components that should be attached.
     */
    template<typename... Components>
    void attach(entity id, Components&&... components);

    /** Removes all components related to the entity.
     * Because the entity is just a numeric identifier, this doesn't actually
     * remove the entity; only all components. You can technically use the same
     * entity again by calling attach() for it.
     * \param id The entity whose components to remove.
     */
    inline void remove(entity id);

    /** Removes a component of an entity.
     * \tparam Component The type of component to remove from the entity.
     * \param id The entity whose component to remove.
     */
    template<typename Component>
    void remove(entity id);

    /** Removes all components of all entities.
     * It also resets the entity counter, so this truly invalidates all
     * previous entities!
     */
    inline void clear_entities();

    /** Creates a system in the ECS.
     * The system is created in-place. Event receivers and emitters are
     * connected automatically here.
     * \tparam System The type of the system to create.
     * \param args Arguments for the constructor of System.
     * \return A reference to the created system.
     */
    template<typename System, typename... Args>
    System& add_system(Args&&... args);

    /** Adds a system to the ECS if it isn't present already.
     * Just calls the default constructor if needed.
     * \tparam System The type of the system to create or find.
     * \return Returns a reference to the found or created system.
     */
    template<typename System>
    System& ensure_system();

    /** Removes all systems.  */
    inline void clear_systems();

    /** Starts batching behaviour for add/remove.
     * If you know you are going to do a lot of modifications to existing
     * entities (i.e. attaching new components to old entities or removing
     * components in any case), you can call start_batch() before that and
     * finish_batch() after to gain a lot of performance. If there's only a
     * couple of modifications, don't bother. Also, if you are within a foreach
     * loop, batching will already be applied.
     */
    inline void start_batch();

    /** Finishes batching behaviour for add/remove and applies the changes.
     * Some batched changes take place immediately, but many are not. After
     * calling finish_batch(), all functions act like you would expect.
     */
    inline void finish_batch();

    /** Counts instances of entities with a specified component.
     * \tparam Component the component type to count instances of.
     * \return The number of entities with the specified component.
     * \note This count is only valid when not batching. It is stuck to the
     * value before batching started.
     */
    template<typename Component>
    size_t count() const;

    /** Checks if an entity has the given component.
     * \tparam Component the component type to check.
     * \param id The id of the entity whose component is checked.
     * \return true if the entity has the given component, false otherwise.
     */
    template<typename Component>
    bool has(entity id) const;

    /** Returns the desired component of an entity.
     * Const version.
     * \tparam Component the component type to get.
     * \param id The id of the entity whose component to fetch.
     * \return A pointer to the component if present, null otherwise.
     */
    template<typename Component>
    const Component* get(entity id) const;

    /** Returns the desired component of an entity.
     * \tparam Component the component type to get.
     * \param id The id of the entity whose component to fetch.
     * \return A pointer to the component if present, null otherwise.
     */
    template<typename Component>
    Component* get(entity id);

    /** Returns the Nth entity of those that have a given component.
     * This is primarily useful for picking an arbitrary entity out of many,
     * like picking a random entity etc.
     * \tparam Component The component type whose entity list is used.
     * \param index The index of the entity id to return.
     * \return The Nth entity id with \p Component.
     * \warning There is no bounds checking. Use count() to be safe.
     * \note This function is only valid when not batching, but it is safe to
     * use with count() during batching as well. You may encounter entities that
     * are pending for removal and will not find entities whose addition is
     * pending.
     */
    template<typename Component>
    entity get_entity(size_t index) const;

private:
    class component_container_base: public system
    {
    public:
        virtual ~component_container_base() = default;

        inline virtual void resolve_pending() = 0;
        inline virtual void remove(ecs& ctx, entity id) = 0;
        inline virtual void clear(ecs& ctx) = 0;
        inline virtual size_t count() const = 0;
    };

    template<typename Component>
    struct foreach_iterator_base;

    template<typename Component>
    class component_container:
        public component_container_base,
        public emitter<remove_component<Component>, add_component<Component>>
    {
        template<typename> friend struct foreach_iterator_base;
    public:
        using event_emitter<remove_component<Component>>::emit;
        using event_emitter<add_component<Component>>::emit;

        struct component_tag
        {
            component_tag();
            component_tag(entity id);
            component_tag(entity id, Component&& c);
            entity id;

            Component* get();
            bool operator<(entity id) const;
        };

        struct component_payload: component_tag
        {
            component_payload();
            component_payload(entity id, Component&& c);
            Component c;

            Component* get();
        };

        struct component_indirect: component_tag
        {
            component_indirect();
            component_indirect(entity id, Component* c);
            std::unique_ptr<Component> c;

            Component* get();
        };

        static constexpr bool use_indirect =
            std::is_base_of_v<ptr_component, Component> ||
            std::is_polymorphic_v<Component>;

        using component_data = std::conditional_t<
            use_indirect, component_indirect,
            std::conditional_t<
                std::is_empty_v<Component>, component_tag, component_payload
            >
        >;

        Component* get(entity id);
        entity get_entity(size_t index) const;

        void native_add(
            ecs& ctx,
            entity id,
            std::conditional_t<use_indirect, Component*, Component&&> c
        );
        void add(ecs& ctx, entity id, Component* c);
        void add(ecs& ctx, entity id, Component&& c);
        void add(ecs& ctx, entity id, const Component& c);
        void remove(ecs& ctx, entity id) override;
        void reserve(size_t count);
        void resolve_pending() override;
        void clear(ecs& ctx) override;
        size_t count() const override;

    private:
        std::vector<component_data> components;
        std::vector<entity> pending_removal;
        std::vector<component_data> pending_addition;
    };

    template<typename Component>
    struct foreach_iterator_base
    {
        using Type = std::decay_t<Component>;
        using Container = component_container<Type>;

        foreach_iterator_base(Container& c);

        bool finished();
        inline void advance_up_to(entity id);
        void id();
        typename std::vector<
            typename Container::component_data
        >::iterator begin;
        typename std::vector<
            typename Container::component_data
        >::iterator end;
    };

    template<typename Component>
    struct foreach_iterator;

    template<typename Component>
    struct foreach_iterator<Component&>: foreach_iterator_base<Component>
    {
        static constexpr bool required = true;
        using foreach_iterator_base<Component>::foreach_iterator_base;
        using Type = typename foreach_iterator_base<Component>::Type;

        Type& get(entity id);
    };

    template<typename Component>
    struct foreach_iterator<Component*>: foreach_iterator_base<Component>
    {
        static constexpr bool required = false;
        using foreach_iterator_base<Component>::foreach_iterator_base;
        using Type = typename foreach_iterator_base<Component>::Type;

        Type* get(entity id);
    };

    template<bool pass_id, typename... Components>
    struct foreach_impl
    {
        template<typename F>
        static inline void call(ecs& ctx, F&& f);
    };

    template<typename... Components>
    foreach_impl<true, Components...>
    foreach_redirector(const std::function<void(entity id, Components...)>&);

    template<typename... Components>
    foreach_impl<false, Components...>
    foreach_redirector(const std::function<void(Components...)>&);

    template<typename Component>
    void try_attach_dependencies(entity id);

    template<typename Component>
    component_container<Component>& get_container() const;

    inline void resolve_pending();

    template<typename Component>
    static size_t get_type_key();
    inline static size_t type_key_counter = 0;

    entity id_counter;
    int defer_batch;
    mutable std::vector<std::unique_ptr<component_container_base>> components;
    struct system_data
    {
        std::unique_ptr<system> ptr;
        std::function<void(system& emitter)> connect_all_receivers;
    };
    std::vector<system_data> systems;
};

/** Components may derive from this class to require other components.
 * The other components are added to the entity along with this one if they
 * are not yet present.
 */
template<typename... DependencyComponents>
class dependency_components
{
friend class ecs;
public:
    static void ensure_dependency_components_exist(entity id, ecs& ctx);
};

/** Components may derive from this class to require specific systems.
 * If the system does not yet exist in the ECS, it is created.
 * Typically, dependency systems should just ensure validity of data. For
 * example, if components hold pointers to each other, they should have a
 * dependency to a system that updates those pointers when a component is
 * removed so that no dangling pointers are left.
 */
template<typename... DependencySystems>
class dependency_systems
{
friend class ecs;
public:
    static void ensure_dependency_systems_exist(ecs& ctx);
};

//==============================================================================
// Implementation
//==============================================================================

ecs::ecs()
: id_counter(0), defer_batch(0)
{
    components.reserve(64);
}

ecs::~ecs()
{
    // This is called manually so that remove events are fired if necessary.
    clear_entities();
}

template<typename Component>
ecs::foreach_iterator_base<Component>::foreach_iterator_base(Container& c)
: begin(c.components.begin()), end(c.components.end())
{
}

template<typename Component>
bool ecs::foreach_iterator_base<Component>::finished()
{
    return begin == end;
}

template<typename Component>
void ecs::foreach_iterator_base<Component>::advance_up_to(entity id)
{
    auto last = begin + std::min(entity(end - begin), (id - begin->id));
    begin = std::lower_bound(begin + 1, last, id);
}

template<typename Component>
auto ecs::foreach_iterator<Component&>::get(entity) -> Type&
{
    return *foreach_iterator_base<Component>::begin->get();
}

template<typename Component>
auto ecs::foreach_iterator<Component*>::get(entity id) -> Type*
{
    if(
        foreach_iterator_base<Component>::begin ==
        foreach_iterator_base<Component>::end
    ) return nullptr;

    auto& it = foreach_iterator_base<Component>::begin;
    if(it->id != id) return nullptr;
    return it->get();
}

template<bool pass_id, typename... Components>
template<typename F>
void ecs::foreach_impl<pass_id, Components...>::call(ecs& ctx, F&& f)
{
    ctx.start_batch();

    std::tuple<foreach_iterator<Components>...>
        component_it(ctx.get_container<
            std::decay_t<std::remove_pointer_t<std::decay_t<Components>>>
        >()...);
#define monkero_apply_tuple(...) \
    std::apply([&](auto&... it){return (__VA_ARGS__);}, component_it)

    // Note that all checks based on it.required are compile-time, it's
    // constexpr!
    constexpr bool all_optional = monkero_apply_tuple(!it.required && ...);

    if constexpr(sizeof...(Components) == 1)
    {
        // If we're only iterating one category, we can do it very quickly!
        auto& iter = std::get<0>(component_it);
        while(!iter.finished())
        {
            entity cur_id = iter.begin->id;
            if constexpr(pass_id) f(cur_id, iter.get(cur_id));
            else f(iter.get(cur_id));
            ++iter.begin;
        }
    }
    else if constexpr(all_optional)
    {
        // If all are optional, iteration logic has to differ a bit. The other
        // version would never quit as there would be zero finished required
        // iterators.
        while(monkero_apply_tuple(!it.finished() || ...))
        {
            entity cur_id = monkero_apply_tuple(std::min({
                (it.begin == it.end ?
                 std::numeric_limits<entity>::max() : it.begin->id)...
            }));
            if constexpr(pass_id) monkero_apply_tuple(f(cur_id, it.get(cur_id)...));
            else monkero_apply_tuple(f(it.get(cur_id)...));
            monkero_apply_tuple(
                (it.begin != it.end && it.begin->id == cur_id
                 ? (++it.begin, void()) : void()), ...
            );
        }
    }
    else
    {
        // This is the generic implementation for when there's multiple
        // components where some are potentially optional.
        while(monkero_apply_tuple((!it.finished() || !it.required) && ...))
        {
            entity cur_id = monkero_apply_tuple(
                std::max({(it.required ? it.begin->id : 0)...})
            );
            // Check if all entries have the same id. For each entry that
            // doesn't, advance to the next id.
            bool all_required_equal = monkero_apply_tuple(
                (it.required ?
                    (it.begin->id == cur_id ?
                        true : (it.advance_up_to(cur_id), false)) : 
                    (it.begin == it.end || it.begin->id >= cur_id ?
                        true : (it.advance_up_to(cur_id), true))) && ...
            );
            if(all_required_equal)
            {
                if constexpr(pass_id)
                    monkero_apply_tuple(f(cur_id, it.get(cur_id)...));
                else monkero_apply_tuple(f(it.get(cur_id)...));
                monkero_apply_tuple((it.required ? ++it.begin, void(): void()), ...);
            }
        }
    }
#undef monkero_apply_tuple

    ctx.finish_batch();
}

template<typename T, typename=void>
struct has_ensure_dependency_components_exist: std::false_type { };

template<typename T>
struct has_ensure_dependency_components_exist<
    T,
    decltype((void)
        T::ensure_dependency_components_exist(entity(), *(ecs*)nullptr), void()
    )
> : std::true_type { };

template<typename T, typename=void>
struct has_ensure_dependency_systems_exist: std::false_type { };

template<typename T>
struct has_ensure_dependency_systems_exist<
    T,
    decltype((void)
        T::ensure_dependency_systems_exist(*(ecs*)nullptr), void()
    )
> : std::true_type { };

template<typename Component>
void ecs::try_attach_dependencies(entity id)
{
    (void)id;
    if constexpr(has_ensure_dependency_components_exist<Component>::value)
        Component::ensure_dependency_components_exist(id, *this);
}

template<typename F>
void ecs::foreach(F&& f)
{
    // This one little trick lets us know the argument types without
    // actually using the std::function wrapper at runtime!
    decltype(
        foreach_redirector(std::function(f))
    )::call(*this, std::forward<F>(f));
}

template<typename F>
void ecs::operator()(F&& f)
{
    foreach(std::forward<F>(f));
}

entity ecs::add()
{
    return id_counter++;
}

template<typename... Components>
entity ecs::add(Components&&... components)
{
    entity id = add();
    attach(id, std::forward<Components>(components)...);
    return id;
}

template<typename... Components>
void ecs::attach(entity id, Components&&... components)
{
    (try_attach_dependencies<Components>(id), ...);

    (
        get_container<Components>().add(
            *this, id, std::forward<Components>(components)
        ), ...
    );
}

void ecs::start_batch()
{
    ++defer_batch;
}

void ecs::finish_batch()
{
    if(defer_batch > 0)
    {
        --defer_batch;
        if(defer_batch == 0)
            resolve_pending();
    }
}

template<typename Component>
size_t ecs::count() const
{
    return get_container<Component>().count();
}

template<typename Component>
bool ecs::has(entity id) const
{
    return get<Component>(id) != nullptr;
}

template<typename Component>
const Component* ecs::get(entity id) const
{
    return get_container<Component>().get(id);
}

template<typename Component>
Component* ecs::get(entity id)
{
    return get_container<Component>().get(id);
}

template<typename Component>
entity ecs::get_entity(size_t index) const
{
    return get_container<Component>().get_entity(index);
}

template<typename T, typename=void>
struct has_connect_all_receivers: std::false_type { };

template<typename T>
struct has_connect_all_receivers<
    T,
    decltype((void)
        std::declval<T>().connect_all_receivers(*(system*)nullptr), void()
    )
> : std::true_type { };

template<typename System, typename... Args>
System& ecs::add_system(Args&&... args)
{
    System* sys_ptr = new System(std::forward<Args>(args)...);
    std::function<void(system& emitter)> sys_connect_all_receivers;
    if constexpr(has_connect_all_receivers<System>::value)
        sys_connect_all_receivers = std::bind(
            &System::connect_all_receivers, sys_ptr, std::placeholders::_1
        );

    // Connect ECS-spawned emitters
    if(sys_connect_all_receivers)
        sys_connect_all_receivers(*this);

    // Connect component container emitters
    for(auto& container: components)
    {
        if(sys_connect_all_receivers && container)
            sys_connect_all_receivers(*container);
    }

    // Connect the new system to all other systems.
    for(auto& sys: systems)
    {
        if(sys.connect_all_receivers)
            sys.connect_all_receivers(*sys_ptr);
        if(sys_connect_all_receivers)
            sys_connect_all_receivers(*sys.ptr);
    }

    systems.push_back(
        {std::unique_ptr<system>(sys_ptr), std::move(sys_connect_all_receivers)}
    );

    return *sys_ptr;
}

template<typename System>
System& ecs::ensure_system()
{
    for(auto& sys: systems)
        if(System* s = dynamic_cast<System*>(sys.ptr.get()))
            return *s;
    return add_system<System>();
}

void ecs::remove(entity id)
{
    for(auto& c: components)
        if(c) c->remove(*this, id);
}

template<typename Component>
void ecs::remove(entity id)
{
    get_container<Component>().remove(*this, id);
}

void ecs::clear_entities()
{
    for(auto& c: components)
        if(c) c->clear(*this);
    id_counter = 0;
}

void ecs::clear_systems()
{
    systems.clear();
}

template<typename Component>
void ecs::reserve(size_t count)
{
    get_container<Component>().reserve(count);
}

template<typename Component>
ecs::component_container<Component>::component_tag::component_tag()
{
}

template<typename Component>
ecs::component_container<Component>::component_tag::component_tag(entity id)
: id(id)
{
}

template<typename Component>
ecs::component_container<Component>::component_tag::component_tag(
    entity id, Component&&
): id(id)
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_tag::get()
{
    // This is safe because the component_tag type is only used when Component
    // is an empty struct! They cannot have real data so just giving a pointer
    // to the id should be fine.
    return reinterpret_cast<Component*>(&id);
}

template<typename Component>
bool ecs::component_container<Component>::component_tag::operator<(
    entity id
) const
{
    return this->id < id;
}

template<typename Component>
ecs::component_container<Component>::component_payload::component_payload()
{
}

template<typename Component>
ecs::component_container<Component>::component_payload::component_payload(
    entity id, Component&& c
): component_tag(id), c(std::move(c))
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_payload::get()
{
    return &c;
}

template<typename Component>
ecs::component_container<Component>::component_indirect::component_indirect()
{
}

template<typename Component>
ecs::component_container<Component>::component_indirect::component_indirect(
    entity id, Component* c
): component_tag(id), c(c)
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_indirect::get()
{
    return c.get();
}

template<typename Component>
Component* ecs::component_container<Component>::get(entity id)
{
    // Check if this entity is pending for removal. If so, it doesn't really
    // exist anymore.
    auto rit = std::lower_bound(
        pending_removal.begin(), pending_removal.end(), id
    );
    if(rit != pending_removal.end() && *rit == id)
        return nullptr;

    // Check if pending_addition has it.
    auto it = std::lower_bound(
        pending_addition.begin(), pending_addition.end(), id
    );
    if(it != pending_addition.end() && it->id == id)
        return it->get();

    // Finally, check the big components vector has it.
    it = std::lower_bound(components.begin(), components.end(), id);
    if(it != components.end() && it->id == id)
        return it->get();
    return nullptr;
}

template<typename Component>
entity ecs::component_container<Component>::get_entity(size_t index) const
{
    return components[index].id;
}

template<typename Component>
void ecs::component_container<Component>::native_add(
    ecs& ctx,
    entity id,
    std::conditional_t<use_indirect, Component*, Component&&> c
){
    if(ctx.defer_batch)
    {
        // Check if this entity is already pending for removal. Remove from
        // that vector first if so.
        auto rit = std::lower_bound(
            pending_removal.begin(), pending_removal.end(), id);
        if(rit != pending_removal.end() && *rit == id)
            pending_removal.erase(rit);

        // Then, add to pending_addition too, if not there yet.
        auto it = std::lower_bound(
            pending_addition.begin(), pending_addition.end(), id);
        if(it == pending_addition.end() || it->id != id)
        {
            // Skip the search if nobody cares.
            if(event_emitter<remove_component<Component>>::subscriber_count())
            {
                // If this entity already exists in the components, signal the
                // removal of the previous one.
                auto it = std::lower_bound(
                    components.begin(), components.end(), id
                );
                if(it != components.end() && it->id == id)
                    emit(ctx, remove_component<Component>{id, it->get()});
            }

            emit(ctx, add_component<Component>{
                id, pending_addition.emplace(it, id, std::move(c))->get()
            });
        }
        else
        {
            emit(ctx, remove_component<Component>{id, it->get()});
            *it = component_data(id, std::move(c));
            emit(ctx, add_component<Component>{id, it->get()});
        }
    }
    else
    {
        // If we can take the fast path of just dumping at the back, do it.
        if(components.size() == 0 || components.back().id < id)
        {
            emit(ctx, add_component<Component>{
                id, components.emplace_back(id, std::move(c)).get()
            });
        }
        else
        {
            auto it = std::lower_bound(
                components.begin(), components.end(), id);
            if(it->id != id)
                emit(ctx, add_component<Component>{
                    id, components.emplace(it, id, std::move(c))->get()
                });
            else
            {
                emit(ctx, remove_component<Component>{id, it->get()});
                *it = component_data(id, std::move(c));
                emit(ctx, add_component<Component>{id, it->get()});
            }
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, Component* c
){
    if constexpr(use_indirect)
        native_add(ctx, id, c);
    else
    {
        native_add(ctx, id, std::move(*c));
        delete c;
    }
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, Component&& c
){
    if constexpr(use_indirect)
        native_add(ctx, id, new Component(std::move(c)));
    else
        native_add(ctx, id, std::move(c));
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, const Component& c
){
    if constexpr(use_indirect)
        native_add(ctx, id, new Component(c));
    else
        native_add(ctx, id, Component(c));
}

template<typename Component>
void ecs::component_container<Component>::reserve(size_t count)
{
    components.reserve(count);
    pending_removal.reserve(count);
    pending_addition.reserve(count);
}

template<typename Component>
void ecs::component_container<Component>::remove(ecs& ctx, entity id)
{
    bool do_emit = 
        event_emitter<remove_component<Component>>::subscriber_count();

    if(ctx.defer_batch)
    {
        // Check if this entity is already pending for addition. Remove from
        // there first if so.
        auto ait = std::lower_bound(
            pending_addition.begin(), pending_addition.end(), id);
        if(ait != pending_addition.end() && ait->id == id)
        {
            if(do_emit)
            {
                component_data tmp = std::move(*ait);
                pending_addition.erase(ait);
                emit(ctx, remove_component<Component>{id, tmp.get()});
            }
            else pending_addition.erase(ait);
        }

        // Then, add to proper removal too, if not there yet.
        auto it = std::lower_bound(
            pending_removal.begin(), pending_removal.end(), id);
        if(it == pending_removal.end() || *it != id)
        {
            pending_removal.insert(it, id);
            if(do_emit)
            {
                auto cit = std::lower_bound(
                    components.begin(), components.end(), id);
                if(cit != components.end() && cit->id == id)
                    emit(ctx, remove_component<Component>{id, cit->get()});
            }
        }
    }
    else
    {
        auto it = std::lower_bound(
            components.begin(), components.end(), id);
        if(it != components.end() && it->id == id)
        {
            if(do_emit)
            {
                component_data tmp = std::move(*it);
                components.erase(it);
                emit(ctx, remove_component<Component>{id, tmp.get()});
            }
            else components.erase(it);
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::resolve_pending()
{
    // Start by removing.
    if(pending_removal.size() != 0)
    {
        auto pit = pending_removal.begin();
        // Start iterating from the last element <= first id to be removed.
        // This could just be components.begin(), but this should help with
        // performance in the common-ish case where most transient entities are
        // also the most recently added ones.
        auto rit = std::lower_bound(components.begin(), components.end(), *pit);
        auto wit = rit;
        int removed_count = 0;

        while(pit != pending_removal.end() && rit != components.end())
        {
            // If this id is equal, this is the entry that should be removed.
            if(*pit == rit->id)
            {
                pit++;
                rit++;
                removed_count++;
            }
            // Skip all pending removals that aren't be in the list.
            else if(*pit < rit->id)
            {
                pit++;
            }
            else if(rit->id < *pit)
            {
                // Compact the vector.
                if(wit != rit)
                    *wit = std::move(*rit);

                // Advance to the next entry.
                ++wit;
                ++rit;
            }
        }

        if(removed_count != 0)
            while(rit != components.end())
            {
                *wit = std::move(*rit);
                ++wit;
                ++rit;
            }

        components.resize(components.size()-removed_count);
        pending_removal.clear();
    }

    // There are two routes for addition, the fast one is for adding at the end,
    // which is the most common use case.
    if(
        pending_addition.size() != 0 && (
            components.size() == 0 ||
            components.back().id < pending_addition.front().id
        )
    ){  // Fast route, only used when all additions are after the last
        // already-extant entity.
        size_t needed_size = components.size() + pending_addition.size();
        if(components.capacity() < needed_size)
            components.reserve(std::max(components.capacity() * 2, needed_size));
        for(component_data& d: pending_addition)
            components.emplace_back(std::move(d));
        pending_addition.clear();
    }
    else if(pending_addition.size() != 0)
    { // Slow route, handles duplicates and interleaved additions.
        // Handle duplicates first.
        {
            auto pit = pending_addition.begin();
            auto wit = components.begin();
            while(pit != pending_addition.end() && wit != components.end())
            {
                if(pit->id == wit->id)
                {
                    *wit = std::move(*pit);
                    pit = pending_addition.erase(pit);
                    ++wit;
                }
                else if(pit->id < wit->id) ++pit;
                else ++wit;
            }
        }

        // If something is still left, actually perform additions.
        if(pending_addition.size() != 0)
        {
            components.resize(components.size() + pending_addition.size());

            auto pit = pending_addition.rbegin();
            auto wit = components.rbegin();
            auto rit = wit+pending_addition.size();

            while(pit != pending_addition.rend() && rit != components.rend())
            {
                if(pit->id > rit->id)
                {
                    *wit = std::move(*pit);
                    ++pit;
                }
                else
                {
                    *wit = std::move(*rit);
                    ++rit;
                }
                ++wit;
            }

            while(pit != pending_addition.rend())
            {
                *wit = std::move(*pit);
                ++pit;
                ++wit;
            }
            pending_addition.clear();
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::clear(ecs& ctx)
{
    bool do_emit =
        event_emitter<remove_component<Component>>::subscriber_count();

    if(ctx.defer_batch)
    {
        // If batching, we can't actually clear everything now. We will simply
        // have to queue everything for removal.
        for(component_data& d: pending_addition) remove(ctx, d.id);
        for(component_data& d: components) remove(ctx, d.id);
    }
    else if(!do_emit)
    {
        // If we aren't going to emit anything and we don't batch, life is easy.
        components.clear();
        pending_removal.clear();
        pending_addition.clear();
    }
    else
    {
        // The most difficult case, we don't batch but we still need to emit.
        std::vector<component_data> tmp(std::move(components));
        components.clear();

        for(component_data& d: tmp)
            emit(ctx, remove_component<Component>{d.id, d.get()});
    }
}

template<typename Component>
size_t ecs::component_container<Component>::count() const
{
    return components.size();
}

template<typename Component>
ecs::component_container<Component>& ecs::get_container() const
{
    size_t key = get_type_key<Component>();
    if(components.size() <= key) components.resize(key+1);
    auto& base_ptr = components[key];
    if(!base_ptr) 
    {
        base_ptr.reset(new component_container<Component>());

        // Connect all systems to the new component container.
        for(auto& sys: systems)
        {
            if(sys.connect_all_receivers)
                sys.connect_all_receivers(*base_ptr);
        }

        if constexpr(has_ensure_dependency_systems_exist<Component>::value)
            Component::ensure_dependency_systems_exist(*const_cast<ecs*>(this));
    }
    return *static_cast<component_container<Component>*>(base_ptr.get());
}

void ecs::resolve_pending()
{
    for(auto& c: components)
        if(c) c->resolve_pending();
}

template<typename Component>
size_t ecs::get_type_key()
{
    static size_t key = type_key_counter++;
    return key;
}

template<typename... DependencyComponents>
void dependency_components<DependencyComponents...>::
ensure_dependency_components_exist(entity id, ecs& ctx)
{
    ((ctx.has<DependencyComponents>(id) ? void() : ctx.attach(id, DependencyComponents())), ...);
}

template<typename... DependencySystems>
void dependency_systems<DependencySystems...>::
ensure_dependency_systems_exist(ecs& ctx)
{
    (ctx.ensure_system<DependencySystems>(), ...);
}

template<typename EventType>
event_receiver<EventType>::event_receiver()
{
}

template<typename EventType>
event_receiver<EventType>::~event_receiver()
{
    for(auto* emitter: subscriptions)
    {
        auto it = std::find(
            emitter->subscribers.begin(), emitter->subscribers.end(), this
        );
        if(it != emitter->subscribers.end())
            emitter->subscribers.erase(it);
    }
}

template<typename... ReceiveEvents>
void receiver<ReceiveEvents...>::connect_all_receivers(
    system& emitter
){
    (try_connect<ReceiveEvents>(emitter), ...);
}

template<typename... ReceiveEvents>
template<typename Event>
void receiver<ReceiveEvents...>::try_connect(system& emitter)
{
    auto* ptr = dynamic_cast<event_emitter<Event>*>(&emitter);
    if(ptr) ptr->subscribe(*static_cast<event_receiver<Event>*>(this));
}

template<typename... EmitEvents>
template<typename EventType>
void emitter<EmitEvents...>::emit(ecs& ctx, const EventType& event)
{
    // See the header for this, I know this looks dumb.
    event_emitter<EventType>::emit(ctx, event);
}

template<typename EventType>
event_emitter<EventType>::event_emitter()
{
}

template<typename EventType>
event_emitter<EventType>::~event_emitter()
{
    for(auto* receiver: subscribers)
        receiver->subscriptions.erase(this);
}

template<typename EventType>
void event_emitter<EventType>::emit(ecs& ctx, const EventType& event)
{
    for(auto* receiver: subscribers)
        receiver->handle(ctx, event);
}
template<typename EventType>
size_t event_emitter<EventType>::subscriber_count() const
{
    return subscribers.size();
}

template<typename EventType>
void event_emitter<EventType>::subscribe(event_receiver<EventType>& s)
{
    auto it = std::find(subscribers.begin(), subscribers.end(), &s);
    if(it == subscribers.end()) subscribers.push_back(&s);
    s.subscriptions.insert(this);
}

}

#endif

