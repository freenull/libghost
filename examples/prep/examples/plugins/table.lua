macro("table", function (param)
    local n_cols = nil

    local cur_col = 0
    local cur_row = 1
    local rows = { {} }

    for elem in param:gmatch("%s*([^#]+)%s*") do
        elem = trim(elem)
        if n_cols == nil then
            n_cols = tonumber(elem)
        elseif cur_col >= n_cols then
            cur_col = 1
            cur_row = cur_row + 1
            table.insert(rows, { elem })
        else
            cur_col = cur_col + 1
            table.insert(rows[cur_row], elem)
        end
    end

    local max_col_widths = {}
    for i = 1, n_cols do
        max_col_widths[i] = 0
    end

    for row_idx, row in ipairs(rows) do
        for col, cell in ipairs(row) do
            local max_width = max_col_widths[col]

            if #cell > max_width then
                max_col_widths[col] = #cell
            end
        end
    end

    local max_row_width = 0
    for row_idx, row in ipairs(rows) do
        local total_row_width = 0

        for col, width in ipairs(max_col_widths) do
            total_row_width = total_row_width + width
        end

        if total_row_width > max_row_width then
            max_row_width = total_row_width
        end
    end

    local line_width = max_row_width + n_cols + 2 * n_cols - 1

    write(" ")
    write(string.rep("-", line_width))
    write("\n")
    for row_idx, row in ipairs(rows) do
        for col, cell in ipairs(row) do
            if col > 1 then
                write(" ")
            end
            write("| ")
            write(cell)
            if #cell < max_col_widths[col] then
                local remaining = max_col_widths[col] - #cell
                write(string.rep(" ", remaining))
            end
        end
        write(" |\n")

        local c = "-"
        if row_idx == 1 then
            c = "="
        end

        local extra_width = 2
        if row_idx == #rows then
            write(" ")
            extra_width = 0
        end

        write(string.rep(c, line_width + extra_width))
        write("\n")
    end
end)
